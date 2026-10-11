<#
.SYNOPSIS
Assembles a release directory from a built Windows installer.

.DESCRIPTION
Takes the installer the packaging step produced, copies it into
`<checkout>\output\releases\<version>\`, makes sure the copy can run with the
rights of a normal user, and writes the `SHA256SUMS.txt` next to it.

The permission part is not optional: an installer that carries a reduced
integrity label starts with reduced rights and fails immediately with
"Setup was unable to create the directory ...\Temp\is-XXXX.tmp. Error 5: Access
denied", because it cannot unpack itself into %TEMP%. Files created inside a
restricted session inherit such a label, and a plain copy made there inherits it
too, so the release step verifies and repairs the copy instead of trusting it.
Verification and repair need a normal (unelevated) session; the script reports
the exact command and fails when it cannot fix the copy.

.PARAMETER Version
Release version, for example 2026.0.23.1.

.PARAMETER Installer
Installer to publish. Defaults to
`<workspace>\dist\mengshee-<version>\Mengshee-<version>-Setup.exe`.

.PARAMETER OutDir
Release directory. Defaults to `<checkout>\output\releases\<version>`.

.PARAMETER WorkspaceRoot
Build workspace holding `dist\`; detected when omitted.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Version,
    [string] $Installer = "",
    [string] $OutDir = "",
    [string] $WorkspaceRoot = ""
)

$ErrorActionPreference = "Stop"

$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if (!$WorkspaceRoot) {
    foreach ($candidate in @((Join-Path $sourceRoot "windows-build-workspace"), (Join-Path (Split-Path -Parent $sourceRoot) "windows_build"))) {
        if (Test-Path -LiteralPath (Join-Path $candidate "dist")) {
            $WorkspaceRoot = $candidate
            break
        }
    }
}
if (!$Installer) {
    if (!$WorkspaceRoot) {
        throw "Pass -Installer or -WorkspaceRoot: no build workspace with a dist directory was found."
    }
    $Installer = Join-Path $WorkspaceRoot "dist\mengshee-$Version\Mengshee-$Version-Setup.exe"
}
if (!(Test-Path -LiteralPath $Installer -PathType Leaf)) {
    throw "Missing installer: $Installer"
}
$Installer = [System.IO.Path]::GetFullPath($Installer)
if (!$OutDir) {
    $OutDir = Join-Path $sourceRoot "output\releases\$Version"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

$name = "Mengshee-$Version-Setup.exe"
$published = Join-Path $OutDir $name
Copy-Item -LiteralPath $Installer -Destination $published -Force

$makeRunnable = Join-Path $PSScriptRoot "make-runnable.ps1"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $makeRunnable -Root $OutDir -Probe $name -Check
if ($LASTEXITCODE -ne 0) {
    Write-Host "The published copy cannot run with normal user rights; repairing it."
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $makeRunnable -Root $OutDir -Probe $name
    if ($LASTEXITCODE -ne 0) {
        throw "The published installer keeps a reduced integrity label and would fail to install. Run this in a normal (unelevated) PowerShell and retry:`n  icacls `"$published`" /setintegritylevel Medium"
    }
}

$hash = (Get-FileHash -LiteralPath $published -Algorithm SHA256).Hash.ToLower()
"$hash  $name" | Set-Content -Path (Join-Path $OutDir "SHA256SUMS.txt") -Encoding ASCII

$size = (Get-Item -LiteralPath $published).Length
Write-Host "Published: $published"
Write-Host "  size   : $size bytes"
Write-Host "  sha256 : $hash"
Write-Host "  sums   : $(Join-Path $OutDir 'SHA256SUMS.txt')"
exit 0
