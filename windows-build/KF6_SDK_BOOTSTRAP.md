# Mengshee KF6 SDK Bootstrap

This path builds the KF6 development SDK used by the Windows standalone Mengshee
build. The default version is the stable KF6 tag `v6.27.0`.

All output goes under the sibling workspace:

```text
..\windows_build\sources
..\windows_build\build\kf6-sdk
..\windows_build\sdk
```

## Bootstrap ECM

From the repository root:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/bootstrap-kf6-sdk.cmake
```

To pin a different stable KF6 release:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -DKF6_VERSION=6.26.0 `
  -P windows-build/cmake/bootstrap-kf6-sdk.cmake
```

The script writes `mengshee-sdk-bootstrap.json` and `mengshee-sdk-env.cmake` into
the SDK prefix so later build steps reuse the same Qt, CMake, Ninja, and MSVC
paths.

## Support Libraries

Build zlib and freetype before modules that need compression or font support:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/build-zlib-sdk.cmake

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/build-freetype-sdk.cmake
```

Install the libintl compatibility shim used by KI18n on Windows:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/install-gettext-native-sdk.cmake

& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/build-libintl-shim-sdk.cmake
```

The native gettext install provides the real Windows `msgfmt.exe` used to
compile `.mo` catalogs. The libintl shim exports the gettext symbols needed by
KI18n and still provides minimal fallback tools, but it is not a full gettext
replacement.

Install pinned flex and bison wrappers for modules that generate parsers:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/install-winflexbison-sdk.cmake
```

## Build KF6 Modules

After ECM is installed, build individual stable KF6 modules with:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DMODULE=kcoreaddons `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/build-kf6-module.cmake
```

The script records installed modules in:

```text
..\windows_build\sdk\mengshee-sdk-modules.cmake
```

Some modules need extra CMake options. For example, build `KArchive` with
nonessential compression and crypto backends disabled:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DMODULE=karchive `
  -DEXTRA_CMAKE_ARGS="-DWITH_BZIP2=OFF,-DWITH_LIBLZMA=OFF,-DWITH_OPENSSL=OFF,-DWITH_LIBZSTD=OFF" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/build-kf6-module.cmake
```

## Build Poppler

After zlib and freetype are installed in the SDK, build custom Poppler:

```powershell
& "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
  -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
  -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
  -DWORKSPACE_ROOT=C:/Users/jairy/Documents/okular/windows_build `
  -P windows-build/cmake/build-poppler-sdk.cmake
```

Mengshee uses the installed Poppler libraries from `..\windows_build\sdk` and
the generated private Poppler headers from
`..\windows_build\build\thirdparty\poppler-custom`.

`external/poppler` is a locally modified, pinned fork rather than an arbitrary
upstream checkout. After changing its gitlink, rebuild both the Poppler SDK and
the Mengshee PDF generator; do not combine private headers or libraries from an
older submodule revision with the current `PdfPageSequenceEditor` sources. See
`docs/local-poppler-fork.md` for the patch boundary and update checklist.
