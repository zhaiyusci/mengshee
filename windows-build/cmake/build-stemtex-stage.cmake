cmake_minimum_required(VERSION 3.20)

# Builds the versioned StemTeX stage that the Mengshee runtime deployment needs.
#
# The stage is assembled from the pinned StemTeX source in external/stemtex:
#
#   engine      texlive-xetex/build-standalone-msvc.sh (bash + MSVC) produces
#               xetexdaemon, dvipdfmxdaemon and xdvipdfmxdaemon
#   dvisvgm     texlive-xetex/build-dvisvgmdaemon-msvc.ps1 produces the SVG daemon
#   stage       CMake configure/build/install of the StemTeX project into
#               <WORKSPACE_ROOT>/stemtex/StemTeX-<version>
#
# The daemon side tree (dist/stemtex-texlive-daemon-static) is only assembled
# when the source tree ships scripts/stage-stemtex.sh, which the pinned revision
# does not. It only supplies the optional prebuilt xelatexdaemon.fmt; without it
# the renderer generates the format in the user cache on first use.
#
# Example:
#   & "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
#     -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
#     -DWORKSPACE_ROOT=C:/path/to/checkout/windows_build `
#     -P windows-build/cmake/build-stemtex-stage.cmake

get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_windows_build_dir "${_script_dir}/.." ABSOLUTE)
get_filename_component(_default_source_root "${_windows_build_dir}/.." ABSOLUTE)
get_filename_component(_default_workspace_root "${_default_source_root}/../windows_build" ABSOLUTE)
include("${_script_dir}/kf6-sdk-common.cmake")

mengshee_default_path(SOURCE_ROOT "${_default_source_root}")
mengshee_default_path(WORKSPACE_ROOT "${_default_workspace_root}")
mengshee_default_path(SDK_PREFIX "${WORKSPACE_ROOT}/sdk")
mengshee_find_toolchain()

if(NOT DEFINED JOBS OR "${JOBS}" STREQUAL "")
    set(JOBS 8)
endif()
if(NOT DEFINED STEMTEX_JOBS OR "${STEMTEX_JOBS}" STREQUAL "")
    set(STEMTEX_JOBS 6)
endif()
if(NOT DEFINED STEMTEX_TEXLIVE_ROOT OR "${STEMTEX_TEXLIVE_ROOT}" STREQUAL "")
    # texmf-dist source for the bundled, trimmed TeX tree.
    set(STEMTEX_TEXLIVE_ROOT "C:/texlive/2026")
endif()
if(NOT DEFINED STEMTEX_ROOT OR "${STEMTEX_ROOT}" STREQUAL "")
    set(STEMTEX_ROOT "${SOURCE_ROOT}/external/stemtex")
endif()
get_filename_component(STEMTEX_ROOT "${STEMTEX_ROOT}" ABSOLUTE)

if(NOT EXISTS "${STEMTEX_ROOT}/VERSION")
    message(FATAL_ERROR "Cannot find the StemTeX source tree: ${STEMTEX_ROOT}")
endif()
file(READ "${STEMTEX_ROOT}/VERSION" _stemtex_version)
string(STRIP "${_stemtex_version}" STEMTEX_VERSION)

if(NOT DEFINED STEMTEX_STAGE_ROOT OR "${STEMTEX_STAGE_ROOT}" STREQUAL "")
    set(STEMTEX_STAGE_ROOT "${WORKSPACE_ROOT}/stemtex/StemTeX-${STEMTEX_VERSION}")
endif()
get_filename_component(STEMTEX_STAGE_ROOT "${STEMTEX_STAGE_ROOT}" ABSOLUTE)

set(_standalone "${STEMTEX_ROOT}/texlive-xetex/out/standalone-msvc")
set(_side_tree "${STEMTEX_ROOT}/dist/stemtex-texlive-daemon-static")
set(_stemtex_build "${WORKSPACE_ROOT}/build/stemtex-ninja")
set(_tools_dir "${WORKSPACE_ROOT}/tools")

message(STATUS "Mengshee StemTeX stage build")
message(STATUS "  Source    : ${STEMTEX_ROOT} (${STEMTEX_VERSION})")
message(STATUS "  Stage     : ${STEMTEX_STAGE_ROOT}")
message(STATUS "  Workspace : ${WORKSPACE_ROOT}")
message(STATUS "  TeXLive   : ${STEMTEX_TEXLIVE_ROOT}")

# ---------------------------------------------------------------------------
# MSVC discovery for the engine scripts
# ---------------------------------------------------------------------------

# The engine scripts locate vcvars64.bat through vswhere. On machines where
# vswhere does not report the installed Visual Studio (for example a Visual
# Studio 18 instance whose state.json has no isComplete/isLaunchable), they
# answer with a small shim that prints the installation path. The proper fix is
# a StemTeX-side option to pass vcvars explicitly; until then this keeps a
# from-scratch build working without patching the submodule.
function(mengshee_detect_vs_install out_var)
    if(DEFINED VS_INSTALL_DIR AND NOT "${VS_INSTALL_DIR}" STREQUAL "")
        set(${out_var} "${VS_INSTALL_DIR}" PARENT_SCOPE)
        return()
    endif()
    # The instance metadata lives under %ProgramData%; some environments do not
    # export that variable, so fall back to the other spellings of the same
    # directory before giving up.
    set(_program_data_roots "")
    if(DEFINED ENV{ProgramData} AND NOT "$ENV{ProgramData}" STREQUAL "")
        list(APPEND _program_data_roots "$ENV{ProgramData}")
    endif()
    if(DEFINED ENV{ALLUSERSPROFILE} AND NOT "$ENV{ALLUSERSPROFILE}" STREQUAL "")
        list(APPEND _program_data_roots "$ENV{ALLUSERSPROFILE}")
    endif()
    list(APPEND _program_data_roots "C:/ProgramData")
    list(REMOVE_DUPLICATES _program_data_roots)

    foreach(_root IN LISTS _program_data_roots)
        file(GLOB _instances "${_root}/Microsoft/VisualStudio/Packages/_Instances/*/state.json")
        foreach(_state IN LISTS _instances)
            file(READ "${_state}" _json)
            string(REGEX MATCH "\"installationPath\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\"" _match "${_json}")
            if(CMAKE_MATCH_1)
                set(_path "${CMAKE_MATCH_1}")
                string(REPLACE "\\\\" "\\" _path "${_path}")
                if(EXISTS "${_path}/VC/Auxiliary/Build/vcvars64.bat")
                    set(${out_var} "${_path}" PARENT_SCOPE)
                    return()
                endif()
            endif()
        endforeach()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

find_program(_vswhere NAMES vswhere.exe
    HINTS "C:/Program Files (x86)/Microsoft Visual Studio/Installer" "C:/Program Files/Microsoft Visual Studio/Installer")
set(_vswhere_shim "")
set(_vswhere_cmd "")
if(_vswhere)
    execute_process(
        COMMAND "${_vswhere}" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        OUTPUT_VARIABLE _vswhere_output ERROR_QUIET RESULT_VARIABLE _vswhere_result)
    string(STRIP "${_vswhere_output}" _vswhere_output)
endif()

if(NOT _vswhere_output)
    mengshee_detect_vs_install(_vs_install)
    if(_vs_install STREQUAL "")
        message(FATAL_ERROR
            "vswhere cannot report a Visual Studio installation and no instance was found under\n"
            "  $ENV{ProgramData}/Microsoft/VisualStudio/Packages/_Instances\n"
            "Pass -DVS_INSTALL_DIR=<Visual Studio directory> or fix vswhere discovery.")
    endif()
    message(STATUS "vswhere does not report ${_vs_install}; using the vswhere shims in ${_tools_dir}")
    file(MAKE_DIRECTORY "${_tools_dir}")
    set(_vswhere_shim "${_tools_dir}/vswhere.sh")
    set(_vswhere_cmd "${_tools_dir}/vswhere.cmd")
    file(WRITE "${_vswhere_shim}"
"#!/bin/sh
# Generated by windows-build/cmake/build-stemtex-stage.cmake.
# vswhere on this machine does not report the Visual Studio installation, so the
# StemTeX engine scripts are pointed here; the arguments they pass are ignored
# and the installation path is printed.
echo '${_vs_install}'
")
    file(CHMOD "${_vswhere_shim}" PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    file(WRITE "${_vswhere_cmd}"
"@echo off
rem Generated by windows-build/cmake/build-stemtex-stage.cmake; see vswhere.sh.
echo ${_vs_install}
")
endif()

# ---------------------------------------------------------------------------
# bash for the engine scripts
# ---------------------------------------------------------------------------

find_program(MENGSHEE_BASH NAMES bash.exe
    HINTS "C:/Program Files/Git/bin" "C:/Program Files/Git/usr/bin"
          "C:/msys64/usr/bin" "C:/cygwin64/bin")
if(NOT MENGSHEE_BASH)
    message(FATAL_ERROR
        "The XeTeX engine scripts are bash scripts. Install Git for Windows, MSYS2 or Cygwin\n"
        "and pass -DMENGSHEE_BASH=<bash.exe> if it is not in a default location.")
endif()

file(TO_CMAKE_PATH "${STEMTEX_ROOT}" _stemtex_fwd)
string(REGEX REPLACE "^([A-Za-z]):" "/\\1" _stemtex_msys "${_stemtex_fwd}")
string(SUBSTRING "${_stemtex_msys}" 1 1 _drive)
string(TOLOWER "${_drive}" _drive_lower)
string(SUBSTRING "${_stemtex_msys}" 2 -1 _stemtex_tail)
set(_stemtex_msys "/${_drive_lower}${_stemtex_tail}")

# ---------------------------------------------------------------------------
# engine binaries
# ---------------------------------------------------------------------------

set(_engine_files
    xetexdaemon.dll xetexdaemon.exe
    dvipdfmxdaemon.dll xdvipdfmxdaemon.exe
    dvisvgmdaemon.dll dvisvgmdaemon.exe)

set(_engine_missing FALSE)
foreach(_engine_file IN LISTS _engine_files)
    if(NOT EXISTS "${_standalone}/${_engine_file}")
        set(_engine_missing TRUE)
    endif()
endforeach()

if(_engine_missing OR (DEFINED FORCE AND FORCE))
    message(STATUS "[run ] stemtex engine (bash + MSVC)")
    if(_vswhere_shim STREQUAL "")
        execute_process(COMMAND "${MENGSHEE_BASH}" -lc
            "cd '${_stemtex_msys}' && JOBS=${STEMTEX_JOBS} ./texlive-xetex/build-standalone-msvc.sh"
            RESULT_VARIABLE _engine_result)
    else()
        execute_process(COMMAND "${CMAKE_COMMAND}" -E env "VSWHERE=${_vswhere_shim}"
            "${MENGSHEE_BASH}" -lc
            "cd '${_stemtex_msys}' && JOBS=${STEMTEX_JOBS} ./texlive-xetex/build-standalone-msvc.sh"
            RESULT_VARIABLE _engine_result)
    endif()
    if(NOT _engine_result EQUAL 0)
        message(FATAL_ERROR "Building the XeTeX engine failed with exit code ${_engine_result}.")
    endif()
else()
    message(STATUS "[skip] stemtex engine: binaries already present in ${_standalone}")
endif()

if(NOT EXISTS "${_standalone}/dvisvgmdaemon.dll" OR (DEFINED FORCE AND FORCE))
    message(STATUS "[run ] stemtex dvisvgm daemon")
    set(_dvisvgm_args -OutDir "${_standalone}")
    if(NOT _vswhere_cmd STREQUAL "")
        list(APPEND _dvisvgm_args -VSWhere "${_vswhere_cmd}")
    endif()
    execute_process(COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
        -File "${STEMTEX_ROOT}/texlive-xetex/build-dvisvgmdaemon-msvc.ps1" ${_dvisvgm_args}
        RESULT_VARIABLE _dvisvgm_result)
    if(NOT _dvisvgm_result EQUAL 0)
        message(FATAL_ERROR "Building the dvisvgm daemon failed with exit code ${_dvisvgm_result}.")
    endif()
else()
    message(STATUS "[skip] stemtex dvisvgm daemon: already present")
endif()

foreach(_engine_file IN LISTS _engine_files)
    if(NOT EXISTS "${_standalone}/${_engine_file}")
        message(FATAL_ERROR "The StemTeX engine is incomplete, missing ${_standalone}/${_engine_file}.")
    endif()
endforeach()

if(EXISTS "${STEMTEX_ROOT}/scripts/stage-stemtex.sh")
    message(STATUS "[run ] stemtex daemon side tree")
    execute_process(COMMAND "${MENGSHEE_BASH}" -lc
        "cd '${_stemtex_msys}' && STANDALONE_DIR='${_stemtex_msys}/texlive-xetex/out/standalone-msvc' STEMTEX_SIDE_TREE='${_stemtex_msys}/dist/stemtex-texlive-daemon-static' ./texlive-xetex/install-msvc-standalone-to-side-tree.sh"
        RESULT_VARIABLE _side_tree_result)
    if(NOT _side_tree_result EQUAL 0)
        message(FATAL_ERROR "Assembling the StemTeX daemon side tree failed with exit code ${_side_tree_result}.")
    endif()
else()
    message(STATUS "[skip] stemtex daemon side tree: scripts/stage-stemtex.sh is not part of this StemTeX revision")
    message(STATUS "       the renderer will generate xelatexdaemon.fmt in the user cache on first use")
endif()

# ---------------------------------------------------------------------------
# StemTeX application stage
# ---------------------------------------------------------------------------

set(_stage_version_file "${STEMTEX_STAGE_ROOT}/runtime/VERSION")
if(EXISTS "${_stage_version_file}" AND NOT (DEFINED FORCE AND FORCE))
    file(READ "${_stage_version_file}" _staged_version)
    string(STRIP "${_staged_version}" _staged_version)
    message(STATUS "[skip] stemtex stage: ${STEMTEX_STAGE_ROOT} already contains version ${_staged_version}")
    return()
endif()

if(DEFINED FORCE AND FORCE)
    mengshee_remove_inside("${STEMTEX_STAGE_ROOT}" "${WORKSPACE_ROOT}/stemtex")
endif()

mengshee_prepare_build_dir("${_stemtex_build}" "${WORKSPACE_ROOT}/build" "${STEMTEX_ROOT}")

set(_python_exe "")
if(DEFINED PYTHON_EXECUTABLE AND NOT "${PYTHON_EXECUTABLE}" STREQUAL "")
    set(_python_exe "${PYTHON_EXECUTABLE}")
else()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "using CMake's Python discovery")
    find_program(_python_program NAMES python.exe python3.exe
        HINTS "C:/Users/$ENV{USERNAME}/AppData/Local/Python/pythoncore-3.14-64")
    if(_python_program)
        set(_python_exe "${_python_program}")
    endif()
endif()

set(_configure
    "\"${CMAKE_PROGRAM}\""
    -S "\"${STEMTEX_ROOT}\""
    -B "\"${_stemtex_build}\""
    -G Ninja
    "-DCMAKE_MAKE_PROGRAM=\"${NINJA_PROGRAM}\""
    -DCMAKE_BUILD_TYPE=Release
    "-DCMAKE_PREFIX_PATH=\"${QT_PREFIX}\""
    "-DCMAKE_INSTALL_PREFIX=\"${STEMTEX_STAGE_ROOT}\""
    "-DSTEMTEX_TEXLIVE_ROOT=\"${STEMTEX_TEXLIVE_ROOT}\""
    "-DSTEMTEX_STANDALONE_DIR=\"${_standalone}\""
    "-DSTEMTEX_RUNTIME_SOURCE=\"${_standalone}\""
)
if(NOT _python_exe STREQUAL "")
    list(APPEND _configure "-DPython3_EXECUTABLE=\"${_python_exe}\"")
endif()
list(JOIN _configure " " _configure_command)

message(STATUS "[run ] stemtex stage")
mengshee_run_vs("${_configure_command}")
mengshee_run_vs("\"${CMAKE_PROGRAM}\" --build \"${_stemtex_build}\" --parallel ${JOBS}")
mengshee_run_vs("\"${CMAKE_PROGRAM}\" --install \"${_stemtex_build}\" --prefix \"${STEMTEX_STAGE_ROOT}\"")

foreach(_required IN ITEMS
        "runtime/VERSION"
        "runtime/bin/sdk/stemtex-renderer.dll"
        "runtime/bin/sdk/stemtex-profile.dll"
        "runtime/bin/windows/stemtex-worker-host.exe"
        "gui/stemtex-profile-creator.exe"
        "gui/profiles/unicodemath")
    if(NOT EXISTS "${STEMTEX_STAGE_ROOT}/${_required}")
        message(FATAL_ERROR "The StemTeX stage is incomplete, missing ${_required} under ${STEMTEX_STAGE_ROOT}.")
    endif()
endforeach()

message(STATUS "StemTeX stage ready: ${STEMTEX_STAGE_ROOT}")
