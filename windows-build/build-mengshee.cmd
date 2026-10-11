@echo off
rem Builds, deploys and packages Mengshee with the canonical CMake driver.
rem
rem Run this from a normal (unelevated) session - double-click it, or use it from
rem a terminal. Ninja cannot start its compilers from a restricted session: it
rem creates the child's standard handles itself, and a session that confines the
rem process tree refuses that, so the build stalls forever with no output. A
rem build started there has to be escalated, which is why this launcher exists:
rem a developer runs it directly and no approval is involved.
rem
rem Extra CMake definitions are passed through, for example:
rem   build-mengshee.cmd -DSKIP_INSTALLER=ON -DSKIP_SMOKE=ON
rem   build-mengshee.cmd -DSTEPS=app
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "WORKSPACE=%ROOT%\windows-build-workspace"
if not defined MENGSHEE_WORKSPACE set "MENGSHEE_WORKSPACE=%WORKSPACE%"

if not defined MENGSHEE_CMAKE (
    set "MENGSHEE_CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
    if not exist "!MENGSHEE_CMAKE!" set "MENGSHEE_CMAKE=cmake"
)
if not exist "%MENGSHEE_WORKSPACE%" mkdir "%MENGSHEE_WORKSPACE%"

rem Keep the console in the encoding the build tree was configured with, so
rem Ninja records /showIncludes dependencies correctly.
chcp 65001 >nul
set "VSLANG=1033"

echo Source    : %ROOT%
echo Workspace : %MENGSHEE_WORKSPACE%
echo.
"%MENGSHEE_CMAKE%" -DWORKSPACE_ROOT="%MENGSHEE_WORKSPACE%" -P "%ROOT%\windows-build\cmake\build-windows.cmake" %*
set "RESULT=%ERRORLEVEL%"

echo.
if "%RESULT%"=="0" (
    echo Build complete. Start the developer runtime printed above, for example
    echo   %MENGSHEE_WORKSPACE%\dist\mengshee-pdf\app\bin\mengshee.exe
) else (
    echo Build FAILED with exit code %RESULT%
)
if not defined MENGSHEE_NOPAUSE pause
exit /b %RESULT%
