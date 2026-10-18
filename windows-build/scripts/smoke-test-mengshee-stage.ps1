[CmdletBinding()]
param(
    [string] $StageRoot = "",
    [string] $PdfPath = "",
    [int] $WaitSeconds = 15
)

$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
# Match the canonical build workspace. -StageRoot explicitly selects another runtime.
$windowsBuildRoot = Join-Path $repoRoot "windows-build-workspace"
if (!$StageRoot) {
    $StageRoot = Join-Path $windowsBuildRoot "dist\mengshee-pdf\app"
}
$StageRoot = [System.IO.Path]::GetFullPath($StageRoot)

if (!$PdfPath) {
    throw "Pass -PdfPath with a local PDF to smoke test."
}

if (!(Test-Path -LiteralPath $PdfPath)) {
    throw "Missing PDF smoke-test file: $PdfPath"
}
$PdfPath = [System.IO.Path]::GetFullPath($PdfPath)

$mengsheeExe = Join-Path $StageRoot "bin\mengshee.exe"
if (!(Test-Path -LiteralPath $mengsheeExe)) {
    throw "Missing staged Mengshee executable: $mengsheeExe"
}

# A staged tree whose files carry a reduced integrity label starts with reduced
# rights: it cannot write %TEMP% or %LOCALAPPDATA%, so saving fails and the KIO
# worker cannot create its socket. That is invisible in a launch-only check, so
# verify the label before starting the application.
$makeRunnable = Join-Path $PSScriptRoot "make-runnable.ps1"
if (Test-Path -LiteralPath $makeRunnable) {
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $makeRunnable -Root $StageRoot -Check
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "The staged runtime cannot be started with normal user rights, so saving would fail for whoever runs it."
        exit 4
    }
}

Get-Process -Name mengshee -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -like "$StageRoot*" } |
    Stop-Process -Force

$oldPath = $env:PATH
$process = $null
try {
    $env:PATH = "$StageRoot\bin;$env:SystemRoot\System32;$env:SystemRoot"
    # Never forward the smoke-test document to another installed/running instance.
    Start-Process -FilePath $mengsheeExe -ArgumentList @("--new-process", "`"$PdfPath`"") -WorkingDirectory (Join-Path $StageRoot "bin") -WindowStyle Minimized
    Start-Sleep -Seconds $WaitSeconds

    $process = Get-Process -Name mengshee -ErrorAction Stop |
        Where-Object { $_.Path -like "$StageRoot*" } |
        Select-Object -First 1

    if (!$process) {
        throw "Staged Mengshee is not running."
    }

    $modules = $process.Modules | ForEach-Object FileName | Where-Object { $_ }
    $unexpectedWorkspaceModules = $modules | Where-Object {
        $_ -like "$windowsBuildRoot\*" -and $_ -notlike "$StageRoot\*"
    }
    $pdfModules = $modules | Where-Object { $_ -match "okularGenerator|poppler|okularpart" } | Sort-Object

    [pscustomobject]@{
        ProcessId = $process.Id
        Responding = $process.Responding
        MainWindowTitle = $process.MainWindowTitle
        UnexpectedWorkspaceModuleCount = @($unexpectedWorkspaceModules).Count
        PdfModules = $pdfModules
    } | Format-List

    if ($unexpectedWorkspaceModules) {
        Write-Warning "Staged Mengshee loaded project-local modules from outside the stage:"
        $unexpectedWorkspaceModules | Sort-Object | ForEach-Object { Write-Warning "  $_" }
        exit 2
    }

    $pdfGenerator = Join-Path $StageRoot "bin\plugins\okular_generators\okularGenerator_mupdf.dll"
    $requiredPdfModules = @(Join-Path $StageRoot "bin\plugins\kf6\parts\okularpart.dll")
    if (Test-Path -LiteralPath $pdfGenerator) {
        $requiredPdfModules += $pdfGenerator
        if ($modules | Where-Object { $_ -match '\\poppler[^\\]*\.dll$|okularGenerator_poppler\.dll$' }) {
            throw "MuPDF runtime loaded a stale Poppler module."
        }
    } else {
        $pdfGenerator = Join-Path $StageRoot "bin\plugins\okular_generators\okularGenerator_poppler.dll"
        $requiredPdfModules += @(Join-Path $StageRoot "bin\poppler.dll"; Join-Path $StageRoot "bin\poppler-qt6.dll")
    }
    $missingPdfModules = $requiredPdfModules | Where-Object { $pdfModules -notcontains $_ }
    if ($missingPdfModules) {
        Write-Warning "Required staged PDF modules were not loaded:"
        $missingPdfModules | ForEach-Object { Write-Warning "  $_" }
        exit 3
    }

    if ($pdfModules -notcontains $pdfGenerator) {
        Write-Warning "PDF generator module was not visible in the process module list. If the PDF opened successfully, this can be a loader-timing false negative."
    }

    exit 0
}
finally {
    $env:PATH = $oldPath
    if ($process) {
        $process.CloseMainWindow() | Out-Null
        Start-Sleep -Seconds 2
        if (Get-Process -Id $process.Id -ErrorAction SilentlyContinue) {
            Stop-Process -Id $process.Id -Force
        }
    }
}
