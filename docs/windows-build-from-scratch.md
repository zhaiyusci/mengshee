# Windows build from scratch

How to build the whole Windows runtime — SDK, StemTeX, application, installer —
from a fresh clone, with CMake as the only driver. The single entry point is
`windows-build/cmake/build-windows-from-scratch.cmake`.

Everything lands inside the checkout by default:

```text
<checkout>\windows-build-workspace\
  downloads\                     pinned archives (gettext, winflexbison, poppler-data)
  sources\                       KF6 module checkouts
  tools\pyvenv\                  Python venv with lxml for breeze-icons
  build\kf6-sdk\...              KF6 module build trees
  build\thirdparty\...           zlib, freetype, libintl, poppler, poppler-data, tesseract
  build\stemtex-ninja\           StemTeX build tree
  sdk\                           SDK prefix (KF6, Qt helpers, poppler, tesseract, ...)
  stemtex\StemTeX-<version>\     StemTeX stage consumed by the runtime deployment
  install\mengshee\              installed application tree
  dist\mengshee-pdf\app\         staged runtime (launch bin\mengshee.exe)
  dist\mengshee-pdf\optional\    optional bundled StemTeX TeX tree component
  dist\installer\                Mengshee-<version>-Setup.exe
```

`windows-build-workspace/` is ignored by git, and `smoke-test-mengshee-stage.ps1` finds
both this layout and the older sibling-workspace layout.

## Prerequisites (not in the repository)

| Requirement | Notes |
| --- | --- |
| Visual Studio with the x64 C++ toolchain | `VCVARS` defaults to the Visual Studio 18 Community path; override with `-DVCVARS=...` |
| Qt for MSVC | e.g. `C:/Qt/6.11.1/msvc2022_64`; `Qt6TextToSpeech` is optional and not used |
| CMake 3.20+ and Ninja | the Qt Tools copies are used by default |
| Git for Windows, MSYS2 or Cygwin | the XeTeX engine scripts are bash scripts; `-DMENGSHEE_BASH=<bash.exe>` if not in a default location |
| Python 3 | only to create the workspace venv and install `lxml`; `-DPYTHON_EXECUTABLE=<python.exe>` to choose it |
| TeX Live 2026 | `C:/texlive/2026/texmf-dist` supplies the trimmed TeX tree; `-DSTEMTEX_TEXLIVE_ROOT=<root>` |
| Inno Setup 6 | only for the installer; `-DSKIP_INSTALLER=ON` to stop after staging |
| Network access | pinned downloads: extra-cmake-modules, KF6 modules, gettext, winflexbison, poppler-data, tesseract sources |

## Build

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -P windows-build/cmake/build-windows-from-scratch.cmake
```

Useful switches:

| Switch | Effect |
| --- | --- |
| `-DWORKSPACE_ROOT=<dir>` | keep the workspace outside the checkout (the older sibling layout) |
| `-DSTEPS=sdk;stemtex;app;smoke` | run a subset; the same list is accepted by `bootstrap-windows-sdk.cmake` (`python`, `ecm`, `gettext`, `winflexbison`, `zlib`, `freetype`, `libintl`, `kf6`, `poppler`, `poppler-data`, `tesseract`) |
| `-DFORCE=ON` | redo the selected steps from scratch |
| `-DJOBS=<n>` | parallel build jobs |
| `-DSKIP_INSTALLER=ON` | build and stage the runtime without Inno Setup |
| `-DSKIP_SMOKE=ON` | skip the staged smoke test |
| `-DPYTHON_EXECUTABLE=<python.exe>` | base interpreter for the workspace venv |
| `-DVS_INSTALL_DIR=<dir>` | Visual Studio location when `vswhere` cannot report it |

Individual steps, for debugging one part of the pipeline:

```powershell
# SDK only (skips everything already present)
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
  -P windows-build/cmake/bootstrap-windows-sdk.cmake

# StemTeX stage only
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
  -P windows-build/cmake/build-stemtex-stage.cmake

# Poppler CMap/CID data only, redoing it
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
  -DSTEPS=poppler-data -DFORCE=ON `
  -P windows-build/cmake/bootstrap-windows-sdk.cmake
```

Every step is idempotent: it is skipped when its result is already present, so
re-running the driver is how you pick up a source change after the first build.

## What each step does

1. **python** – creates `windows-build-workspace/tools/pyvenv` and installs `lxml`. KF6's
   `breeze-icons` generates its 24x24 icon set with `tools/generate-24px-versions.py`,
   which imports `lxml`; the icons are compiled into `KF6BreezeIcons.dll`.
   `-DWITH_ICON_GENERATION=OFF` would avoid Python, at the cost of the 24x24
   icon set, so Python is kept.
2. **ecm / gettext / winflexbison** – ECM, native `msgfmt.exe`, and the pinned
   flex/bison plus the bison data directory (`m4sugar`, `skeletons`, `xslt`) that
   parser-generating modules need.
3. **zlib / freetype / libintl / poppler / tesseract** – the SDK libraries.
4. **kf6** – every module in `windows-build/manifests/kf6-modules.txt`, in
   dependency order. The manifest also carries the per-module configure flags
   that upstream needs on Windows (see below).
5. **poppler-data** – the separate upstream Poppler CMap/CID data release,
   installed into `sdk/share/poppler`. `package-windows.cmake` validates
   `share/poppler/cMap/Adobe-GB1/UniGB-UTF16-H` and
   `share/poppler/cidToUnicode/Adobe-GB1`, and CJK/encoded PDFs need the data at
   run time.
6. **stemtex** – builds the XeTeX/xdvipdfmx/dvisvgm daemons from
   `external/stemtex/texlive-xetex` with bash + MSVC, then configures, builds and
   installs the StemTeX project into `windows-build-workspace/stemtex/StemTeX-<version>`.
7. **app** – `build-windows.cmake`: configure, build and install Mengshee,
   deploy the runtime from SDK/Qt/StemTeX/Mengshee data, stage
   `dist/mengshee-pdf/app`, and build the installer.
8. **smoke** – launches the staged `mengshee.exe` with a test PDF and checks the
   modules it loaded come from the staged tree.

## KF6 module manifest

`windows-build/manifests/kf6-modules.txt` lists the modules and their Windows
configure flags:

```text
kcoreaddons
karchive EXTRA=-DWITH_BZIP2=OFF,-DWITH_LIBLZMA=OFF,-DWITH_OPENSSL=OFF,-DWITH_LIBZSTD=OFF
ktextwidgets EXTRA=-DWITH_TEXT_TO_SPEECH=OFF
snoretoast GIT_URL=https://github.com/KDE/snoretoast.git GIT_REF=v0.8.0
```

- `karchive` needs `-DWITH_*=OFF`: it otherwise requires `PkgConfig` and the
  optional compression/crypto backends, which the runtime takes from Qt.
- `ktextwidgets` requires `Qt6TextToSpeech` unless speech support is disabled;
  the pinned Qt kit does not ship that module.
- `snoretoast` is not an `invent.kde.org/frameworks` module.

The driver adds `-DPython3_EXECUTABLE`/`-DPython_EXECUTABLE` for the workspace
venv to every module, through `EXTRA_CMAKE_ARGS`, because
`build-kf6-module.cmake` only forwards `EXTRA_CMAKE_ARGS` to the module
configure.

## Known constraints

- **Visual Studio discovery**: the StemTeX engine scripts look up
  `vcvars64.bat` through `vswhere`. When `vswhere` does not report the installed
  instance (`state.json` without `isComplete`/`isLaunchable`, as with some
  Visual Studio 18 installs), `build-stemtex-stage.cmake` derives the
  installation path from `state.json` and writes `windows-build-workspace/tools/vswhere.sh`
  and `vswhere.cmd` shims that the engine scripts are pointed at. `-DVS_INSTALL_DIR`
  overrides the detection. The durable fix belongs in StemTeX: accept an explicit
  `VCVARS` instead of only asking `vswhere`.
- **Prebuilt TeX format**: the pinned StemTeX revision has no
  `scripts/stage-stemtex.sh`, so the daemon side tree with a prebuilt
  `xelatexdaemon.fmt` is not assembled. The renderer generates the format in the
  user cache on first use; the StemTeX install reports this.
- **Optional TeX tree**: the bundled, trimmed `texmf-dist` comes from
  `C:/texlive/2026`; without a TeX Live installation the install step fails, so
  keep a TeX Live tree available or pass `-DSTEMTEX_TEXLIVE_ROOT`.
- The installer step fails without Inno Setup 6; use `-DSKIP_INSTALLER=ON` for a
  runtime-only result.
