# Mengshee Windows Build

> The default PDF backend is MuPDF. Historical Poppler-specific instructions
> apply only when building with `-DMENGSHEE_PDF_BACKEND=Poppler`.

This directory contains the Windows standalone SDK, runtime deployment, staging,
and installer scripts for this Mengshee checkout.

Run commands from the repository root:

```powershell
cd C:\Users\jairy\Documents\mengshee
```

Build scripts live in the version-controlled `windows-build/` directory.
SDKs, build trees, logs, and runtimes live inside the checkout under
`windows-build-workspace/`, which is ignored by Git. All Windows entry points
use this workspace by default; an explicit workspace argument can override it.

## Layout

- Source tree: `.`
- Build workspace: `windows-build-workspace`
- SDK prefix: `windows-build-workspace\sdk`
- Mengshee build dir: `windows-build-workspace\build\mengshee-standalone`
- Mengshee install dir: `windows-build-workspace\install\mengshee`
- Package stage: `windows-build-workspace\dist\mengshee-pdf\app`
- Package output: `windows-build-workspace\dist`

The install dir and the package stage are both runnable: start
`install\mengshee\bin\mengshee.exe` or `dist\mengshee-pdf\app\bin\mengshee.exe`
to try a build before it is packaged. See
[Running the build before packaging](#running-the-build-before-packaging).

## Requirements

- Windows 10/11 x64.
- MSVC x64 toolchain and Windows SDK. Visual Studio 2022 or newer is fine when
  passed through `-VcVars`.
- Qt for MSVC, for example `C:\Qt\6.11.1\msvc2022_64`.
- Git.
- Inno Setup 6, only when building the installer `.exe`.
- Python 3, only for the workspace venv that `breeze-icons` needs for its 24x24
  icon generation; the from-scratch driver creates the venv and installs `lxml`.
- An installed StemTeX runtime (the published setup, e.g. `C:\StemTeX`), or one
  passed with `-DSTEMTEX_STAGE_ROOT=`. The build uses it in place; only when none
  is found does it download and install the pinned release, which needs
  administrator rights once.
- Initialized third-party sources:

```powershell
git submodule update --init --recursive external/poppler
```

StemTeX is not a submodule. LaTeX notes are rendered by the StemTeX runtime, and
this build consumes the StemTeX project's own artifacts. The runtime is resolved
by `windows-build\cmake\stemtex-common.cmake`, in this order: an explicit
`-DSTEMTEX_STAGE_ROOT`, an installed StemTeX (its registered `InstallLocation`,
then the usual directories), a runtime prepared inside the workspace, and finally
the pinned release, which `windows-build\cmake\prepare-stemtex-runtime.cmake`
downloads and installs into `WORKSPACE_ROOT\stemtex\StemTeX-<version>`. The
resolved runtime is used in place - never rebuilt and never copied into the
workspace.

The same driver sparse-checks-out the QScintilla headers and import library the
LaTeX note editor compiles against, from the StemTeX tag matching the resolved
runtime version, into `WORKSPACE_ROOT\stemtex\qscintilla\third_party`; a version
change refetches them. `-DSTEMTEX_VERSION=<version>` requires a specific release,
`-DSTEMTEX_INSTALLER=<exe>` installs from a local setup, and
`-DSTEMTEX_QSCINTILLA_REF=<tag>` overrides the tag.

Runtime deployment requires `runtime\VERSION`, the renderer/profile DLLs, the
worker host, the profile creator and the bundled tree; profile warm-up files that
an already used runtime carries are excluded from the package.

For release work with a specific StemTeX, install it first (the published setup,
elevated) or produce a stage from a StemTeX build tree and pass it to the driver:

```powershell
$stemtexRoot = "C:\Users\jairy\Documents\xetex\stemtex"
$stemtexVersion = (Get-Content "$stemtexRoot\VERSION").Trim()
$stemtexStage = "C:\Users\jairy\Documents\mengshee\windows-build-workspace\stemtex\StemTeX-$stemtexVersion"

if (Test-Path $stemtexStage) {
  throw "StemTeX stage already exists: $stemtexStage"
}

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" --install `
  "$stemtexRoot\build\stemtex-ninja" --prefix $stemtexStage
```

Build and smoke-test StemTeX first using its own
`docs\BUILDING_AND_PACKAGING.md`. Warm-up output (`.xdv`, `.aux`, `.log`, `.pdf`,
`.synctex.gz`) under a runtime's `gui\profiles` is never packaged, so an
already used runtime is a valid input.

If using a non-default Visual Studio path, pass it to CMake scripts:

```powershell
-DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat"
```

## From-Scratch Build Inside The Checkout

The complete Windows runtime can be produced from a fresh clone with one CMake
driver, which keeps its workspace inside the checkout:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -P windows-build/cmake/build-windows-from-scratch.cmake
```

It bootstraps the SDK, prepares the StemTeX runtime, then runs the canonical
Mengshee build/deploy/package step and smoke-tests the staged runtime. Every
step is skipped when its result already exists, `-DFORCE=ON` redoes the selected
steps and `-DSTEPS=<list>` runs a subset. Prerequisites, the resulting
`windows-build-workspace\` layout and the known constraints are documented in
[Windows build from scratch](../docs/windows-build-from-scratch.md).

## Canonical CMake Build

Use the CMake driver for normal Windows builds:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
  "-DSTEMTEX_STAGE_ROOT=$stemtexStage" `
  -P windows-build/cmake/build-windows.cmake
```

It runs:

1. CMake configure/build/install for Mengshee
2. CMake runtime deployment from the SDK, Qt, StemTeX, and Mengshee data
3. stage refresh under `windows-build-workspace\dist\mengshee-pdf\app`
4. Inno Setup installer build

For a clean Mengshee rebuild, add `-DCLEAN_BUILD=ON`.

The driver uses UTF-8 console output for both compiler detection and builds
(and prefers English MSVC messages when installed). Keep the same encoding when
running manual CMake/Ninja commands: after `vcvars64.bat`, run `chcp 65001` and
`set VSLANG=1033`. Otherwise a localized `/showIncludes` prefix can be recorded
incorrectly, making Ninja miss header dependencies and retain ABI-incompatible
objects. If upgrading from such a build, use `-DCLEAN_BUILD=ON` once; changing the
console encoding alone does not repair the old compiler cache or object files.

If Inno Setup is not installed, still build and stage the runtime with:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -DSKIP_INSTALLER=ON `
  -P windows-build/cmake/build-windows.cmake
```

Expected installer output:

```text
windows-build-workspace\dist\mengshee-<version>\Mengshee-<version>-Setup.exe
```

## Running The Build Before Packaging

The install tree and the package stage are complete runtimes, so a developer can
start one and try a change before it is packaged:

```text
windows-build-workspace\install\mengshee\bin\mengshee.exe
windows-build-workspace\dist\mengshee-pdf\app\bin\mengshee.exe     (the developer runtime)
```

Those trees have to run with the rights of a normal user. Windows starts a process
at the lower of your integrity level and the executable's own label, and build
output created inside a restricted (low integrity) session inherits a low label
from that session. An application started from such a tree looks fine but runs
with reduced rights: it cannot write `%TEMP%` or `%LOCALAPPDATA%`, so

- saving fails with `Could not open the temporary file for saving.`, and
- KIO fails with `Could not create socket for launching a KIO worker for protocol "file".`

The build therefore restores the label itself. `mengshee_prepare_run_tree()`
(`cmake\run-tree-common.cmake`) resets the installed tree after deployment and the
stage before anything is packaged, reads the labels back, and **fails the build**
when it cannot, naming the exact command:

```powershell
icacls "<tree>" /setintegritylevel "(OI)(CI)Medium" /T
```

The label is inherited from the **directory**, so a workspace that sits inside a
reduced-label tree produces reduced-label output even when the build itself runs
normally, and so does anything copied into it — including a published installer.
Keep the workspace and `output\releases` labelled like the rest of the user's
folders, and the check above becomes a no-op:

```powershell
icacls windows-build-workspace /setintegritylevel "(OI)(CI)Medium" /T
icacls output /setintegritylevel "(OI)(CI)Medium" /T
```

Raising a label needs a normal (unelevated) session, so a build that itself runs
restricted cannot repair what it produced. In that case either run that command
once in a normal PowerShell and build again, or pass `-DALLOW_LOW_INTEGRITY=ON`
to record a warning instead of an error and relabel the tree later. For a tree
that already exists, use the script directly - it accepts `-Check` to report
without changing anything:

```powershell
powershell -ExecutionPolicy Bypass -File windows-build\scripts\make-runnable.ps1 -Root windows-build-workspace\dist\mengshee-pdf\app
powershell -ExecutionPolicy Bypass -File windows-build\scripts\make-runnable.ps1 -Root windows-build-workspace\install\mengshee -Check
```

The staged smoke test checks the same thing before it launches the application, so
a runtime that would misbehave for whoever starts it does not pass validation.
Do not point `%TEMP%` at a build directory as a workaround: that only moves the
saving staging file and leaves the KIO and per-user paths broken.

The installer is checked too, against its own `Mengshee-<version>-Setup.exe` in
`OUTPUT_DIR`. An installer that carries a reduced label cannot unpack itself and
fails at once with `Setup was unable to create the directory
"...\Temp\is-XXXX.tmp". Error 5: Access denied`.

Publishing a release publishes that checked file and verifies the copy:

```powershell
powershell -ExecutionPolicy Bypass -File windows-build\scripts\make-release.ps1 -Version 2026.0.23.1
```

It copies the installer into `output\releases\<version>\`, verifies (and, in a
normal session, repairs) the label of the copy, and writes `SHA256SUMS.txt` beside
it. A copy made inside a restricted session inherits a reduced label even when the
source file was fine, which is why the published copy is checked rather than
trusted.

## Run The Build From A Normal Session

`windows-build\build-mengshee.cmd` is the canonical driver with the paths resolved,
so a build is one action instead of a command line to paste:

```powershell
.\windows-build\build-mengshee.cmd                              # build, deploy, package
.\windows-build\build-mengshee.cmd -DSKIP_INSTALLER=ON -DSKIP_SMOKE=ON
.\windows-build\build-mengshee.cmd -DSTEPS=app
```

It passes its arguments through to `build-windows.cmake` and needs no elevation.
Run it from a normal session. **Ninja cannot run inside a session that confines
the process tree**: it creates its children's standard handles itself, and such a
session refuses that, so `ninja` stalls forever on the first command it starts —
even a one-line `cmd /c echo` — while `ninja -n`, which starts nothing, still
works. Nothing about this build is at fault and nothing in the repository can
change it: the same build runs normally outside such a session, and inside one it
has to be escalated. That is why a build is started here by whoever has a normal
session, instead of being driven from a confined shell.

## CMake Package Step

After `windows-build-workspace\install\mengshee` has been built and deployed, the
package-only CMake script can recreate the stage and installer without
rebuilding Mengshee:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DCLEAN_STAGE=ON `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/package-windows.cmake
```

This script mirrors the deployed install tree into
`windows-build-workspace\dist\mengshee-pdf\app`, validates Mengshee icons, gettext
catalogs, annotation resources, and Poppler CMap/CID data, then calls Inno
Setup. The main installer keeps the StemTeX renderer binaries and only the
maintained `unicodemath` base profile in its required core component. The TeX
package/font tree is staged
under `windows-build-workspace\dist\mengshee-pdf\optional\stemtex-support` and embedded
in the same installer as the optional `Bundled StemTeX TeX tree` component.
The recommended installation type selects it by default; users can choose the
compact type or clear the component when they want to use an external TeX tree.

To validate staging without building an installer:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DSKIP_INSTALLER=ON `
  -DCLEAN_STAGE=ON `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/package-windows.cmake
```

To write the installer into a temporary output directory:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DCLEAN_STAGE=ON `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -DOUTPUT_DIR=C:/Users/jairy/Documents/mengshee/windows-build-workspace/tmp/cmake-package-test `
  -P windows-build/cmake/package-windows.cmake
```

## Internal Scripts

These scripts are internal steps. Use them only when debugging a specific part
of the Windows pipeline.

- `cmake\build-windows-from-scratch.cmake`
  - Complete Windows build from a fresh clone: SDK bootstrap, StemTeX runtime
    preparation, Mengshee build/deploy/package and the staged smoke test.
    Workspace defaults to `<checkout>\windows-build-workspace`.
- `cmake\bootstrap-windows-sdk.cmake`
  - Bootstraps the whole SDK prefix in dependency order (Python venv with lxml,
    ECM, gettext, winflexbison, zlib, freetype, libintl, the KF6 modules from
    `manifests\kf6-modules.txt`, Poppler, Poppler data, Tesseract). Idempotent,
    with `-DSTEPS=<list>` and `-DFORCE=ON`.
- `cmake\prepare-stemtex-runtime.cmake`
  - Resolves the StemTeX runtime (an installed StemTeX is used in place, or the
    pinned release is installed into `<workspace>\stemtex\StemTeX-<version>`) and
    fetches the QScintilla headers and import library matching it from the same
    StemTeX tag. Nothing of StemTeX is compiled here.
- `cmake\stemtex-common.cmake`
  - Resolves and validates the StemTeX runtime and pins the fallback release.
- `cmake\build-poppler-data-sdk.cmake`
  - Installs the pinned Poppler CMap/CID data release into
    `sdk\share\poppler`, which the staged runtime and `package-windows.cmake`
    require.
- `cmake\build-windows.cmake`
  - Canonical Windows driver for Mengshee configure/build/install, runtime
    deploy, stage refresh, and installer creation.
- `cmake\deploy-runtime.cmake`
  - CMake-based runtime deployment from an installed Mengshee tree.
- `cmake\package-windows.cmake`
  - CMake-based stage and installer creation from an already deployed
    `windows-build-workspace\install\mengshee` tree.
  - Builds one Mengshee installer containing the bundled StemTeX TeX tree as an
    optional installation component.
- `cmake\bootstrap-kf6-sdk.cmake`
  - CMake-based ECM bootstrap for the local Windows SDK.
- `cmake\build-kf6-module.cmake`
  - CMake-based KF6 module checkout, configure, build, and install driver.
- `cmake\build-zlib-sdk.cmake`, `cmake\build-freetype-sdk.cmake`,
  `cmake\build-libintl-shim-sdk.cmake`, `cmake\build-poppler-sdk.cmake`,
  `cmake\build-tesseract-sdk.cmake`
  - CMake-based support library builders.
- `cmake\install-gettext-native-sdk.cmake`,
  `cmake\install-winflexbison-sdk.cmake`
  - CMake-based pinned binary tool installers. The winflexbison installer also
    places the bison data directory next to the tools and verifies it.
- `manifests\kf6-modules.txt`
  - The KF6 module list in dependency order, with the Windows configure flags
    each module needs.
- `smoke-test-mengshee-stage.ps1`
  - Starts staged `mengshee.exe` with a test PDF and verifies it loads modules
    from the stage, not from another workspace tree. It defaults to
    `windows-build-workspace/dist/mengshee-pdf/app`; use `-StageRoot` to select
    another runtime explicitly.

The legacy PowerShell build, deploy, and installer entry points have been
removed. Use the CMake drivers above for all Windows builds and packages.

## Stage-Only Refresh

If the install tree has already been built and deployed, use the CMake package
script to refresh the stage without rebuilding the installer:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DSKIP_INSTALLER=ON `
  -DCLEAN_STAGE=ON `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/package-windows.cmake
```

If a staged StemTeX process is still running, stop it before staging:

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'mengshee|stemtex|xetex|xelatex|xetexdaemon' } |
  Stop-Process -Force
```

## Runtime Data Contract

The CMake runtime deployment intentionally mirrors SDK runtime data from
`windows-build-workspace\sdk\bin\data` into `bin\data`. That SDK sync can remove files
installed earlier by CMake, so deployment must restore Mengshee data after the
sync.

The deployed install tree and stage must contain:

- `bin\mengshee.exe`
- `bin\mengshee.ico`
- `bin\data\applications\org.jairy.mengshee.desktop`
- `bin\data\metainfo\org.jairy.mengshee.appdata.xml`
- `bin\data\icons\hicolor\<size>x<size>\apps\mengshee.png`
- `bin\data\mengshee\tools.xml`
- `bin\data\mengshee\toolsQuick.xml`
- `bin\data\mengshee\drawingtools.xml`
- `bin\data\mengshee\pics\annotation-*.svg`
- `bin\data\locale\<lang>\LC_MESSAGES\okular*.mo`
- non-empty KF6 interface catalogs under `bin\data\locale\zh_CN\LC_MESSAGES`,
  including `kconfigwidgets6.mo` and `kxmlgui6.mo` for standard actions and menus
- `share\poppler\cMap\...`
- `share\poppler\cidToUnicode\...`
- `StemTeX\runtime\bin`
- `StemTeX\runtime\VERSION`
- `StemTeX\runtime\bin\sdk\stemtex-profile.dll`
- `StemTeX\gui\profiles\unicodemath`
- `bin\stemtex-profile-creator.exe`

The main stage must not contain `StemTeX\runtime\texmf-dist` or writable
StemTeX cache/state directories such as `StemTeX\runtime\texmf-var\fonts`.
The xelatex daemon format under `StemTeX\runtime\texmf-var\web2c` is a read-only
runtime input and remains in the main package.

Do not rely on the CMake install step alone for a runnable/packageable Windows
tree. Always run runtime deployment, or use the canonical
`windows-build\cmake\build-windows.cmake` entry point. For packaging an already
deployed tree, use `windows-build\cmake\package-windows.cmake`.

## Validation

Check that a built tree can be started with normal user rights (this is what the
build and the smoke test do automatically):

```powershell
powershell -ExecutionPolicy Bypass -File .\windows-build\scripts\make-runnable.ps1 `
  -Root .\windows-build-workspace\dist\mengshee-pdf\app -Check
```

Run the staged smoke test:

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile `
  -File .\windows-build\scripts\smoke-test-mengshee-stage.ps1 `
  -PdfPath .\autotests\data\file2.pdf
```

Quick file checks:

```powershell
$stage = "C:\Users\jairy\Documents\mengshee\windows-build-workspace\dist\mengshee-pdf\app"
Test-Path "$stage\bin\data\mengshee\pics\annotation-latex-note.svg"
Test-Path "$stage\bin\data\locale\zh_CN\LC_MESSAGES\okular.mo"
Test-Path "$stage\share\poppler\cMap\Adobe-GB1\UniGB-UTF16-H"
Test-Path "$stage\bin\data\icons\hicolor\256x256\apps\mengshee.png"
```

## SDK Bootstrap

The whole SDK bootstraps with one CMake driver:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
  -P windows-build/cmake/bootstrap-windows-sdk.cmake
```

It runs the steps below in order, skipping whatever is already installed, and
builds every KF6 module listed in `manifests\kf6-modules.txt`. Use
`-DSTEPS=<list>` for a subset and `-DFORCE=ON` to redo. The individual steps
remain available for debugging:

The standalone build uses a local SDK prefix for ECM, KF6 modules, zlib,
freetype, libintl shim, helper tools, custom Poppler, and Poppler data.

Bootstrap ECM:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/bootstrap-kf6-sdk.cmake
```

Build support libraries and tools:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/install-gettext-native-sdk.cmake

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/install-winflexbison-sdk.cmake

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/build-zlib-sdk.cmake

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/build-freetype-sdk.cmake

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/build-libintl-shim-sdk.cmake
```

Build required KF6 modules into `windows-build-workspace\sdk` with the CMake module
driver:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DMODULE=kcoreaddons `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/build-kf6-module.cmake
```

Use `windows-build\KF6_SDK_BOOTSTRAP.md` for the module list and per-module
notes.

Build custom Poppler:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/build-poppler-sdk.cmake
```

The command builds the pinned local fork in `external/poppler`. When that
submodule changes, its installed libraries, generated private headers, and the
Mengshee PDF generator must be rebuilt together. The generic page-sequence
editor is compiled into the generator even though the Poppler SDK build uses
`ENABLE_UTILS=OFF`. Maintenance details are in
`docs/local-poppler-fork.md`.

Build the cross-platform English OCR runtime and its single bundled model:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/mengshee/windows-build-workspace `
  -P windows-build/cmake/build-tesseract-sdk.cmake
```

This builds pinned Tesseract and Leptonica shared libraries with image-file
codecs disabled (Mengshee supplies rendered page pixels directly), then installs
only `eng.traineddata`. The same Tesseract API is used on Windows and Linux.

`install-gettext-native-sdk.cmake` installs pinned native Windows
`gettext-iconv-windows` tools under
`windows-build-workspace\sdk\tools\gettext-native`. Mengshee uses that `msgfmt.exe` to
compile real gettext catalogs; the libintl shim is only the runtime
compatibility layer needed by KI18n.

## LaTeX Notes

Mengshee supports the StemTeX renderer for LaTeX notes. The standalone Windows
runtime bundles the StemTeX renderer binaries under `StemTeX\runtime\bin` with
only the maintained `unicodemath` base profile under
`StemTeX\gui\profiles\unicodemath`; Mengshee starts that renderer during
application startup.

The annotation settings page launches the bundled StemTeX Profile Creator with
Mengshee-owned paths. The bundled base profile and user profiles have distinct
stable IDs in the selector. User-created profiles are stored below Mengshee's Qt
`AppLocalDataLocation`, under `StemTeX\profiles`; they are never written to the
installation or StemTeX's global user-profile directory.

The TeXLive package/font tree is optional. The single Mengshee installer carries
it as the `Bundled StemTeX TeX tree` component and selects it in the recommended
installation type. Users can omit that component and select their own TeX tree
from `Settings -> Configure Mengshee -> Annotations`. An empty setting does not
probe the system for TeX; it only uses the bundled tree when that component was
installed.

StemTeX runtime state, fontconfig files, caches, traces, rendered-note outputs,
and temporary LaTeX appearance files are written below Mengshee's Qt
`AppLocalDataLocation`. This keeps `C:\Program Files\Mengshee` read-only and
keeps Mengshee data out of StemTeX's standalone application directories.

To inspect TeX rendering logs:

```powershell
Get-Content "$env:LOCALAPPDATA\mengshee\mengshee-tex-debug.log" -Tail 80
```
