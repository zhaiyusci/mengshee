<#
.SYNOPSIS
Makes a built Mengshee tree startable with the rights of a normal user, or says
why it cannot be.

.DESCRIPTION
Build output created inside a restricted (low integrity) session inherits a low
mandatory integrity label, and Windows starts a process at the lower of the
user's integrity level and the executable's label. An application started from
such a tree therefore runs with reduced rights: it cannot write %TEMP% or
%LOCALAPPDATA%, so saving fails with "could not open the temporary file for
saving", the KIO worker cannot create its socket, and per-user files land in
AppData\LocalLow. Nothing about the build output is wrong; only the label is.

This script resets the label of the whole tree to the level the signed-in user
has, reads the labels back, and reports whether the tree is startable with normal
rights. It needs no elevation - only ownership of the files, which a developer
build has - and it is safe to run repeatedly. Use -Check to verify without
changing anything, for example from a build step or a smoke test.

The check never fails open: a mandatory label is recognised by its policy flags
rather than by a localized name, so an unreadable or unexpected label is reported
as not runnable instead of being taken for an unlabelled file.

.PARAMETER Root
The built tree to make runnable, for example
<checkout>\windows-build-workspace\dist\mengshee-pdf\app or ...\install\mengshee.

.PARAMETER Level
Integrity level to restore. Medium is the level of an ordinary unelevated
process and is what an application is expected to run at.

.PARAMETER Check
Only report; never write labels. Exit code is 1 when the tree is not runnable.

.PARAMETER Probe
Tree-relative files whose labels decide whether the tree is runnable. Defaults to
the application executable, because that is the image Windows takes the process
label from.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Root,
    [ValidateSet("Medium", "High")][string] $Level = "Medium",
    [switch] $Check,
    [string[]] $Probe = @("bin\mengshee.exe")
)

$ErrorActionPreference = "Stop"

# icacls prints integrity labels with English level names on the machines this
# build runs on; the SID spellings are accepted too in case a version prints
# them raw. Anything else is reported as unreadable rather than as unlabelled.
$lowNames = @("Low Mandatory Level", "S-1-16-4096")
$mediumNames = @("Medium Mandatory Level", "S-1-16-8192")
$highNames = @("High Mandatory Level", "S-1-16-12288")

function Get-IntegrityLabel([string] $Path)
{
    $output = & icacls $Path 2>&1 | Out-String
    # A mandatory label is the only entry icacls prints with the no-write-up
    # policy flag, so this recognizes a label without matching a translated name.
    if ($output -notmatch '\(NW\)') {
        return "None"
    }
    foreach ($name in $highNames) { if ($output -match [regex]::Escape($name)) { return "High" } }
    foreach ($name in $mediumNames) { if ($output -match [regex]::Escape($name)) { return "Medium" } }
    foreach ($name in $lowNames) { if ($output -match [regex]::Escape($name)) { return "Low" } }
    return "Unknown"
}

function Test-Runnable($Label)
{
    # An unlabelled file already runs at the level of an ordinary process.
    return $Label -eq "None" -or $Label -eq $Level
}

if (!(Test-Path -LiteralPath $Root -PathType Container)) {
    Write-Error "Not a directory: $Root"
    exit 2
}
$Root = [System.IO.Path]::GetFullPath($Root)

$probeFiles = @()
foreach ($relative in $Probe) {
    $probeFiles += [pscustomobject]@{ Relative = $relative; Path = (Join-Path $Root $relative) }
}
$missing = @($probeFiles | Where-Object { !(Test-Path -LiteralPath $_.Path -PathType Leaf) })
if ($missing.Count -gt 0) {
    $names = ($missing | ForEach-Object { $_.Relative }) -join ", "
    Write-Error "Tree does not look like a Mengshee runtime: missing $names under $Root"
    exit 2
}

$before = @{}
foreach ($file in $probeFiles) { $before[$file.Relative] = Get-IntegrityLabel $file.Path }

$setIntegrityCommand = "icacls `"$Root`" /setintegritylevel `"(OI)(CI)$Level`" /T"
$needsFix = @($before.GetEnumerator() | Where-Object { !(Test-Runnable $_.Value) })

if ($needsFix.Count -gt 0 -and !$Check) {
    Write-Host "Restoring the $Level integrity label on $Root"
    Write-Host "  $setIntegrityCommand"
    & icacls $Root /setintegritylevel "(OI)(CI)$Level" /T | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "Could not change the integrity label (icacls exit code $LASTEXITCODE)."
    }
}

Write-Host "Run tree: $Root"
$unrunnable = @()
foreach ($file in $probeFiles) {
    $after = Get-IntegrityLabel $file.Path
    $runnable = Test-Runnable $after
    if (!$runnable) { $unrunnable += $file }
    if ($after -eq $before[$file.Relative]) {
        $note = $after
    } else {
        $note = "$($before[$file.Relative]) -> $after"
    }
    $suffix = ""
    if (!$runnable) { $suffix = "   NOT runnable with normal rights" }
    Write-Host ("  {0,-32} {1}{2}" -f $file.Relative, $note, $suffix)
}

if ($unrunnable.Count -gt 0) {
    Write-Warning "A program started from this tree would run at reduced integrity: it could not write %TEMP% or %LOCALAPPDATA%, so saving fails, an installer cannot unpack itself, and per-user data is redirected under AppData\LocalLow."
    Write-Host "Run this once in a normal (unelevated) PowerShell, then start the application again:"
    Write-Host "  $setIntegrityCommand"
    Write-Host "The label is inherited from the session that created the files, so a build started from a restricted session produces it."
    exit 1
}

Write-Host "Runnable: $(Join-Path $Root $Probe[0])"
exit 0
