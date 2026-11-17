[CmdletBinding()]
param(
    [string] $StageRoot = "",
    [string] $PdfPath = "",
    [int] $WaitSeconds = 15,
    # auto: both engines for a phase-2 stage, the legacy path otherwise.
    # both/mupdf/poppler force a specific phase-2 pass.
    [ValidateSet("auto", "both", "mupdf", "poppler")]
    [string] $Engines = "auto"
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

$binDir = Join-Path $StageRoot "bin"
$generatorDir = Join-Path $binDir "plugins\okular_generators"
$engineDir = Join-Path $binDir "plugins\mengshee_pdf"
$singlePlugin = Join-Path $generatorDir "okularGenerator_mengshee_pdf.dll"
$mupdfModule = Join-Path $engineDir "MengsheePdfEngineMuPdf.dll"
$popplerModule = Join-Path $engineDir "MengsheePdfEnginePoppler.dll"
$mupdfRuntime = Join-Path $binDir "MengsheePdfRuntime.dll"
$mupdfLegacy = Join-Path $generatorDir "okularGenerator_mupdf.dll"
$popplerLegacy = Join-Path $generatorDir "okularGenerator_poppler.dll"
$dualEngineStage = Test-Path -LiteralPath $singlePlugin
$phase2Artifacts = @($singlePlugin, $mupdfModule, $popplerModule) | Where-Object { Test-Path -LiteralPath $_ }
if (!$dualEngineStage -and $phase2Artifacts) {
    Write-Warning "The stage has a partial phase-2 PDF layout (a plugin or engine module is missing):"
    $phase2Artifacts | ForEach-Object { Write-Warning "  $_" }
    exit 3
}
if ($Engines -eq "both" -and !$dualEngineStage) {
    throw "-Engines both requires a phase-2 stage (okularGenerator_mengshee_pdf.dll and plugins\mengshee_pdf)."
}

if ($dualEngineStage) {
    $requiredStageFiles = @(
        $singlePlugin,
        $mupdfModule,
        $popplerModule,
        $mupdfRuntime,
        (Join-Path $binDir "plugins\kf6\parts\okularpart.dll"),
        (Join-Path $binDir "poppler.dll"),
        (Join-Path $binDir "poppler-qt6.dll"),
        (Join-Path $binDir "tesseract55.dll"),
        (Join-Path $binDir "leptonica-1.87.0.dll"),
        (Join-Path $StageRoot "share\poppler\cMap\Adobe-GB1\UniGB-UTF16-H"),
        (Join-Path $StageRoot "share\poppler\cidToUnicode\Adobe-GB1"),
        # The compatibility fallback must stay deployed next to the new plugin.
        $mupdfLegacy,
        $popplerLegacy
    )
} elseif (Test-Path -LiteralPath $mupdfLegacy) {
    $requiredStageFiles = @($mupdfLegacy, $mupdfRuntime)
} elseif (Test-Path -LiteralPath $popplerLegacy) {
    $requiredStageFiles = @($popplerLegacy, (Join-Path $binDir "poppler.dll"), (Join-Path $binDir "poppler-qt6.dll"))
} else {
    throw "Stage contains neither okularGenerator_mengshee_pdf.dll nor a legacy PDF generator: $StageRoot"
}

$missingStageFiles = $requiredStageFiles | Where-Object { !(Test-Path -LiteralPath $_) }
if ($missingStageFiles) {
    Write-Warning "The staged PDF runtime is incomplete:"
    $missingStageFiles | ForEach-Object { Write-Warning "  $_" }
    if ($dualEngineStage) {
        Write-Warning "A phase-2 stage must ship the single plugin, both engine modules, both dependency sets and both legacy plugins."
    }
    exit 3
}

if ($dualEngineStage) {
    switch ($Engines) {
        "mupdf" { $runList = @("mupdf") }
        "poppler" { $runList = @("poppler") }
        default { $runList = @("mupdf", "poppler") }
    }
} else {
    $legacyEngine = if (Test-Path -LiteralPath $mupdfLegacy) { "mupdf" } else { "poppler" }
    if ($Engines -in @("mupdf", "poppler") -and $Engines -ne $legacyEngine) {
        throw "This is a legacy $legacyEngine stage; -Engines $Engines is not available. Rebuild with -DMENGSHEE_PDF_ENGINE_MODULES=ON for a dual-engine stage."
    }
    $runList = @("legacy")
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

function Stop-StagedMengshee {
    if ($script:stagedProcess) {
        $script:stagedProcess.CloseMainWindow() | Out-Null
        Start-Sleep -Seconds 2
        if (Get-Process -Id $script:stagedProcess.Id -ErrorAction SilentlyContinue) {
            Stop-Process -Id $script:stagedProcess.Id -Force
        }
        $script:stagedProcess = $null
    }
}

function Invoke-PdfEnginePass {
    param([string] $EngineId)

    Get-Process -Name mengshee -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -like "$StageRoot*" } |
        Stop-Process -Force

    $previousEngine = $env:MENGSHEE_PDF_ENGINE
    try {
        if ($EngineId -eq "legacy") {
            Remove-Item Env:MENGSHEE_PDF_ENGINE -ErrorAction SilentlyContinue
        } else {
            $env:MENGSHEE_PDF_ENGINE = $EngineId
        }

        # Never forward the smoke-test document to another installed/running instance.
        Start-Process -FilePath $mengsheeExe -ArgumentList @("--new-process", "`"$PdfPath`"") -WorkingDirectory $binDir -WindowStyle Minimized
        Start-Sleep -Seconds $WaitSeconds

        $process = Get-Process -Name mengshee -ErrorAction Stop |
            Where-Object { $_.Path -like "$StageRoot*" } |
            Select-Object -First 1

        if (!$process) {
            throw "Staged Mengshee is not running."
        }
        $script:stagedProcess = $process

        $modules = $process.Modules | ForEach-Object FileName | Where-Object { $_ }
        $unexpectedWorkspaceModules = $modules | Where-Object {
            $_ -like "$windowsBuildRoot\*" -and $_ -notlike "$StageRoot\*"
        }
        $pdfModules = $modules | Where-Object { $_ -match "okularGenerator|poppler|okularpart|MengsheePdfRuntime|MengsheePdfEngine" } | Sort-Object

        if ($dualEngineStage) {
            # Discovery is lazy: the registry derives engine ids from the module
            # file names, but only createEngine()/createSelectedEngine() load a
            # DLL. A pass therefore requires the selected engine module and its
            # own dependency set only; the other engine module must stay out of
            # the process. A broken selection would make Okular fall back to a
            # legacy generator instead, which the fallback check below rejects.
            $selectedEngineModule = if ($EngineId -eq "mupdf") { $mupdfModule } else { $popplerModule }
            $unselectedEngineModule = if ($EngineId -eq "mupdf") { $popplerModule } else { $mupdfModule }
            $requiredPdfModules = @(
                (Join-Path $binDir "plugins\kf6\parts\okularpart.dll"),
                $singlePlugin,
                $selectedEngineModule
            )
            switch ($EngineId) {
                "mupdf" {
                    $requiredPdfModules += $mupdfRuntime
                }
                "poppler" {
                    $requiredPdfModules += @(
                        (Join-Path $binDir "poppler.dll"),
                        (Join-Path $binDir "poppler-qt6.dll")
                    )
                }
            }
        } elseif ($EngineId -eq "legacy" -and (Test-Path -LiteralPath $mupdfLegacy)) {
            $requiredPdfModules = @(
                (Join-Path $binDir "plugins\kf6\parts\okularpart.dll"),
                $mupdfLegacy,
                $mupdfRuntime
            )
        } else {
            $requiredPdfModules = @(
                (Join-Path $binDir "plugins\kf6\parts\okularpart.dll"),
                $popplerLegacy,
                (Join-Path $binDir "poppler.dll"),
                (Join-Path $binDir "poppler-qt6.dll")
            )
        }

        $missingPdfModules = $requiredPdfModules | Where-Object { $pdfModules -notcontains $_ }

        [pscustomobject]@{
            Engine = $EngineId
            ProcessId = $process.Id
            Responding = $process.Responding
            MainWindowTitle = $process.MainWindowTitle
            UnexpectedWorkspaceModuleCount = @($unexpectedWorkspaceModules).Count
            RequiredPdfModules = $requiredPdfModules
            UnselectedEngineModule = $unselectedEngineModule
            MissingPdfModules = $missingPdfModules
            PdfModules = $pdfModules
        } | Format-List

        if ($unexpectedWorkspaceModules) {
            Write-Warning "Staged Mengshee loaded project-local modules from outside the stage:"
            $unexpectedWorkspaceModules | Sort-Object | ForEach-Object { Write-Warning "  $_" }
            exit 2
        }

        if ($dualEngineStage) {
            # If the selected engine could not be created, Okular retries with a
            # lower-priority legacy generator; that load is the observable failure.
            $legacyFallbackModules = @($mupdfLegacy, $popplerLegacy) | Where-Object { $pdfModules -contains $_ }
            if ($legacyFallbackModules) {
                Write-Warning "The $EngineId pass fell back to a legacy PDF generator:"
                $legacyFallbackModules | ForEach-Object { Write-Warning "  $_" }
                exit 3
            }

            # Lazy discovery must leave the other engine module on disk.
            if ($pdfModules -contains $unselectedEngineModule) {
                Write-Warning "The $EngineId pass loaded the unselected engine module; only the selected engine may be loaded:"
                Write-Warning "  $unselectedEngineModule"
                exit 3
            }
        } elseif ($EngineId -eq "legacy" -and (Test-Path -LiteralPath $mupdfLegacy)) {
            if ($modules | Where-Object { $_ -match '\\poppler[^\\]*\.dll$|okularGenerator_poppler\.dll$|MengsheePdfEnginePoppler\.dll$' }) {
                Write-Warning "MuPDF runtime loaded a stale Poppler module."
                exit 3
            }
        }

        if ($missingPdfModules) {
            Write-Warning "Required staged PDF modules were not loaded:"
            $missingPdfModules | ForEach-Object { Write-Warning "  $_" }
            exit 3
        }
    } finally {
        if ($null -eq $previousEngine) {
            Remove-Item Env:MENGSHEE_PDF_ENGINE -ErrorAction SilentlyContinue
        } else {
            $env:MENGSHEE_PDF_ENGINE = $previousEngine
        }
        Stop-StagedMengshee
    }
}

$oldPath = $env:PATH
$script:stagedProcess = $null
try {
    $env:PATH = "$binDir;$env:SystemRoot\System32;$env:SystemRoot"

    foreach ($engineId in $runList) {
        Invoke-PdfEnginePass -EngineId $engineId
    }

    exit 0
}
finally {
    $env:PATH = $oldPath
    Stop-StagedMengshee
}
