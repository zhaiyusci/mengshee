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

`windows-build-workspace/` is ignored by Git. Build, package, release, and smoke-test
entry points all use this directory by default. To use another location, pass
the relevant workspace argument or `-StageRoot` explicitly; no older layout is
automatically detected.

Both runnable trees — `install\mengshee` and `dist\mengshee-pdf\app` — are given
the integrity label of an ordinary process by the build itself, because an
application started from a tree that carries a lower label cannot write `%TEMP%`
and fails to save. A build that cannot restore the label fails with the command
that does; see
[Running the build before packaging](../windows-build/README.md#running-the-build-before-packaging).

## Get the sources

```powershell
git clone https://github.com/zhaiyusci/mengshee.git C:\path\to\mengshee
cd C:\path\to\mengshee
git submodule update --init --recursive external/poppler
```

`external/poppler` is a build input: the pinned Poppler fork, including
`utils/PdfPageSequenceEditor.cc`, which is compiled into the PDF generator.
`external/pdf-base14-fonts` is a vendored tree in this repository, so it needs no
submodule step. The pinned commit is the one the runtime deployment expects; do
not update the submodule independently.

StemTeX is deliberately **not** a submodule. LaTeX notes are rendered by the
StemTeX runtime, and this build consumes the StemTeX project's own release
artifacts instead of building it (see the `stemtex` step below).

## Prerequisites (not in the repository)

| Requirement | Notes |
| --- | --- |
| Visual Studio with the x64 C++ toolchain | `VCVARS` defaults to the Visual Studio 18 Community path; override with `-DVCVARS=...` |
| Qt for MSVC | e.g. `C:/Qt/6.11.1/msvc2022_64`; `Qt6TextToSpeech` is optional and not used |
| CMake 3.20+ and Ninja | the Qt Tools copies are used by default |
| Git | the pinned QScintilla files are fetched with a sparse checkout |
| Python 3 | only to create the workspace venv and install `lxml`; `-DPYTHON_EXECUTABLE=<python.exe>` to choose it |
| Inno Setup 6 | only for the installer; `-DSKIP_INSTALLER=ON` to stop after staging |
| Network access | pinned downloads: extra-cmake-modules, KF6 modules, gettext, winflexbison, poppler-data, tesseract sources, the QScintilla files |
| StemTeX installed (optional) | the build uses an installed StemTeX runtime in place; without one it installs the pinned release, which needs administrator rights once |

## Build

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -P windows-build/cmake/build-windows-from-scratch.cmake
```

With a single Qt installation under `C:/Qt/*/msvc2022_64` and the default Visual
Studio path, the bare command works without arguments; the driver resolves both
itself. Run it from the repository root. The driver prints what it does and
`[skip]` for everything already present; add `2>&1 | Tee-Object .\build.log` to
keep a log of the whole run.

Useful switches:

| Switch | Effect |
| --- | --- |
| `-DWORKSPACE_ROOT=<dir>` | explicitly override the default `<checkout>/windows-build-workspace` |
| `-DSTEPS=sdk;stemtex;app;smoke` | run a subset; the same list is accepted by `bootstrap-windows-sdk.cmake` (`python`, `ecm`, `gettext`, `winflexbison`, `zlib`, `freetype`, `libintl`, `kf6`, `poppler`, `poppler-data`, `tesseract`) |
| `-DFORCE=ON` | redo the selected steps from scratch |
| `-DJOBS=<n>` | parallel build jobs |
| `-DSKIP_INSTALLER=ON` | build and stage the runtime without Inno Setup |
| `-DSKIP_SMOKE=ON` | skip the staged smoke test |
| `-DALLOW_LOW_INTEGRITY=ON` | do not fail when the built trees keep a reduced integrity label; the trees must then be relabelled with `windows-build/scripts/make-runnable.ps1` before they can run normally (see [Running the build before packaging](../windows-build/README.md#running-the-build-before-packaging)) |
| `-DSMOKE_PDF=<file>` | PDF used by the smoke test (default `autotests/data/file2.pdf`) |
| `-DPYTHON_EXECUTABLE=<python.exe>` | base interpreter for the workspace venv |
| `-DVS_INSTALL_DIR=<dir>` | Visual Studio location when `vswhere` cannot report it |
| `-DSTEMTEX_VERSION=<version>` | require a specific StemTeX release; a matching runtime is used, otherwise it is downloaded and installed |
| `-DSTEMTEX_STAGE_ROOT=<dir>` | use a StemTeX runtime from that directory instead of resolving one |
| `-DSTEMTEX_INSTALLER=<exe>` | install from a local `StemTeX-<version>-Setup.exe` instead of downloading it |

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
6. **stemtex** – does not build StemTeX. It uses an installed StemTeX runtime in
   place (its `runtime/VERSION` decides the QScintilla pin), and sparse checks out
   the matching QScintilla headers and import library from the same StemTeX tag
   into `windows-build-workspace/stemtex/qscintilla/third_party`, which the LaTeX
   note editor compiles against. Only when no StemTeX runtime can be found does it
   download the pinned `StemTeX-<version>-Setup.exe` (SHA-256 checked) and install
   it into `windows-build-workspace/stemtex/StemTeX-<version>`; that path needs
   administrator rights once. Warm-up files that an already used runtime carries
   under `gui/profiles` are never packaged.
7. **app** – `build-windows.cmake`: configure, build and install Mengshee,
   deploy the runtime from SDK/Qt/StemTeX/Mengshee data, give the installed tree
   and the stage the integrity label of an ordinary process (they are the trees a
   developer starts, and one built from a restricted session could not save),
   stage `dist/mengshee-pdf/app`, and build the installer.
8. **smoke** – checks that the staged tree can be started with normal user rights,
   then launches the staged `mengshee.exe` with a test PDF and checks the modules
   it loaded come from the staged tree.

Rough timings from the machine this was written on (compile time dominates, and
the first run also downloads everything):

| Part | Time |
| --- | --- |
| `python`, `ecm`, `gettext`, `winflexbison`, `zlib`, `freetype`, `libintl` | under a minute each |
| 27 KF6 modules | ~24 minutes in total (`breeze-icons` alone takes ~2.5 minutes) |
| `poppler`, `poppler-data`, `tesseract` | ~1.5 minutes, seconds, ~2 minutes |
| `stemtex` | seconds when StemTeX is already installed (plus the QScintilla checkout); about a minute plus a 29 MB download when the runtime has to be installed |
| `app` | the first run compiles the whole application (tens of minutes); ~3 minutes incrementally, including deploy and packaging |

Expect roughly 45-90 minutes for a complete first build.

## Run the result

```powershell
& <checkout>\windows-build-workspace\dist\mengshee-pdf\app\bin\mengshee.exe
```

That tree is self-contained; the installer from `dist\installer` is only needed
to install elsewhere. The staged smoke test verifies it without a manual launch:

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile `
  -File windows-build\scripts\smoke-test-mengshee-stage.ps1 `
  -StageRoot <checkout>\windows-build-workspace\dist\mengshee-pdf\app `
  -PdfPath autotests\data\file2.pdf
```

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

- **Where the StemTeX runtime comes from.** It is resolved in this order: an
  explicit `-DSTEMTEX_STAGE_ROOT`, a StemTeX installation (its registered
  `InstallLocation`, then `C:\StemTeX`, `C:\Program Files\StemTeX`,
  `%LOCALAPPDATA%\Programs\StemTeX`), a runtime prepared inside the workspace,
  and finally the pinned release, which is downloaded and installed (that one
  path needs administrator rights, and the driver says so instead of failing on a
  UAC error). A runtime is used in place: it is neither rebuilt nor copied into
  the workspace.
- **QScintilla follows the runtime.** The headers and import library are fetched
  from the StemTeX tag matching the resolved runtime version, and the fetch is
  repeated when the runtime version changes, so headers, import library and the
  shipped DLL always agree. `-DSTEMTEX_QSCINTILLA_REF=` overrides the tag.
- **Warm-up output is not packaged.** A StemTeX runtime that has been used
  contains profile warm-up files (`*.xdv`, `*.aux`, `*.log`, `*.pdf`,
  `*.synctex.gz`) under `gui/profiles`; they are skipped while staging, because
  the renderer regenerates them keyed to the runtime and the selected TeX tree.
- **No TeX Live and no bash.** The bundled, trimmed `texmf-dist` is part of the
  StemTeX release, so neither a TeX Live installation nor a bash environment is
  needed for the build.
- The installer step fails without Inno Setup 6; use `-DSKIP_INSTALLER=ON` for a
  runtime-only result.
