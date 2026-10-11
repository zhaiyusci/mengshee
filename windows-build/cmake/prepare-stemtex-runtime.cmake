cmake_minimum_required(VERSION 3.20)

# Prepares the StemTeX runtime that Mengshee renders LaTeX notes with.
#
# StemTeX is not built here and is not a submodule: the runtime is the Windows
# installer that the StemTeX project publishes, installed silently into the
# workspace, and the QScintilla files the LaTeX note editor compiles against are
# sparse-checked-out from the pinned StemTeX tag:
#
#   <WORKSPACE_ROOT>/stemtex/StemTeX-<version>/   runtime, gui, bundled texmf
#   <WORKSPACE_ROOT>/stemtex/qscintilla/third_party/
#       qscintilla-src/src/Qsci, qscintilla-src/scintilla/include
#       qscintilla-build/release/qscintilla2_qt6.{lib,dll}
#
# Both steps are skipped when their result is present; -DFORCE=ON redoes them.
# Pass -DSTEMTEX_INSTALLER=<StemTeX-<version>-Setup.exe> to use a local copy
# instead of downloading it, or -DSTEMTEX_STAGE_ROOT=<dir> to reuse a runtime
# that is already installed elsewhere.
#
# Example:
#   & "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
#     -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
#     -P windows-build/cmake/prepare-stemtex-runtime.cmake

get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_windows_build_dir "${_script_dir}/.." ABSOLUTE)
get_filename_component(_default_source_root "${_windows_build_dir}/.." ABSOLUTE)
include("${_script_dir}/kf6-sdk-common.cmake")
include("${_script_dir}/stemtex-common.cmake")

mengshee_default_path(SOURCE_ROOT "${_default_source_root}")
mengshee_default_path(WORKSPACE_ROOT "${SOURCE_ROOT}/windows-build-workspace")

mengshee_resolve_stemtex_version(STEMTEX_VERSION)
if(NOT DEFINED STEMTEX_REPO OR "${STEMTEX_REPO}" STREQUAL "")
    set(STEMTEX_REPO "https://github.com/zhaiyusci/stemtex.git")
endif()
if(NOT DEFINED STEMTEX_QSCINTILLA_REF OR "${STEMTEX_QSCINTILLA_REF}" STREQUAL "")
    # QScintilla headers and the import library must match the DLL that the
    # installer of the same version ships.
    set(STEMTEX_QSCINTILLA_REF "v${STEMTEX_VERSION}")
endif()
if(NOT DEFINED STEMTEX_DOWNLOAD_BASE OR "${STEMTEX_DOWNLOAD_BASE}" STREQUAL "")
    set(STEMTEX_DOWNLOAD_BASE "https://github.com/zhaiyusci/stemtex/releases/download/v${STEMTEX_VERSION}")
endif()
if(NOT DEFINED STEMTEX_INSTALLER_SHA256 OR "${STEMTEX_INSTALLER_SHA256}" STREQUAL "")
    # SHA-256 of StemTeX-<STEMTEX_VERSION>-Setup.exe, published with the release.
    if(STEMTEX_VERSION STREQUAL "0.14.0")
        set(STEMTEX_INSTALLER_SHA256 "1db233faade079417ac59233662e842f45045c4c12ca6996288550d3aa49b634")
    else()
        set(STEMTEX_INSTALLER_SHA256 "")
    endif()
endif()
if(NOT DEFINED STEMTEX_SETUP_TYPE OR "${STEMTEX_SETUP_TYPE}" STREQUAL "")
    # full = runtime + gui + bundled texmf; the packaging step needs all three.
    set(STEMTEX_SETUP_TYPE "full")
endif()

mengshee_resolve_stemtex_stage_dir(STEMTEX_STAGE_ROOT "${WORKSPACE_ROOT}" "${STEMTEX_VERSION}")
set(_qscintilla_root "${WORKSPACE_ROOT}/stemtex/qscintilla")
set(_installer_name "StemTeX-${STEMTEX_VERSION}-Setup.exe")
set(_installer "${WORKSPACE_ROOT}/downloads/${_installer_name}")
set(_setup_log "${WORKSPACE_ROOT}/logs/stemtex-setup.log")

message(STATUS "Mengshee StemTeX runtime")
message(STATUS "  Version   : ${STEMTEX_VERSION}")
message(STATUS "  Stage     : ${STEMTEX_STAGE_ROOT}")
message(STATUS "  QScintilla: ${_qscintilla_root}/third_party (${STEMTEX_QSCINTILLA_REF})")
message(STATUS "  Workspace : ${WORKSPACE_ROOT}")

# ---------------------------------------------------------------------------
# StemTeX runtime from the published installer
# ---------------------------------------------------------------------------

set(_stage_ok FALSE)
if(EXISTS "${STEMTEX_STAGE_ROOT}/runtime/VERSION")
    file(READ "${STEMTEX_STAGE_ROOT}/runtime/VERSION" _stage_version)
    string(STRIP "${_stage_version}" _stage_version)
    if(_stage_version STREQUAL STEMTEX_VERSION)
        set(_stage_ok TRUE)
    else()
        message(STATUS "Installed StemTeX runtime is ${_stage_version}, expected ${STEMTEX_VERSION}; reinstalling")
    endif()
endif()

if(_stage_ok AND NOT (DEFINED FORCE AND FORCE))
    message(STATUS "[skip] StemTeX runtime: ${STEMTEX_STAGE_ROOT} already contains ${STEMTEX_VERSION}")
else()
    # The published StemTeX setup requires administrator rights. Detect a
    # non-elevated token first so the failure explains the alternatives instead
    # of surfacing a UAC error.
    execute_process(COMMAND whoami /groups OUTPUT_VARIABLE _token_groups ERROR_QUIET RESULT_VARIABLE _token_result)
    set(_elevated FALSE)
    if(_token_result EQUAL 0 AND _token_groups MATCHES "S-1-16-12288|S-1-16-16384")
        set(_elevated TRUE)
    endif()
    if(NOT _elevated)
        message(FATAL_ERROR
            "Installing StemTeX ${STEMTEX_VERSION} needs administrator rights and this process is not elevated.\n"
            "Choose one:\n"
            "  1. run this driver once from an elevated PowerShell (right click -> Run as administrator),\n"
            "  2. install StemTeX ${STEMTEX_VERSION} yourself and pass -DSTEMTEX_STAGE_ROOT=<installed directory>,\n"
            "  3. build/install it from a StemTeX checkout ('cmake --install <stemtex build> --prefix <stage>') and pass\n"
            "     that directory as -DSTEMTEX_STAGE_ROOT, or\n"
            "  4. pass -DSTEMTEX_INSTALLER=<StemTeX-${STEMTEX_VERSION}-Setup.exe> and run elevated.")
    endif()

    if(DEFINED STEMTEX_INSTALLER AND NOT "${STEMTEX_INSTALLER}" STREQUAL "")
        get_filename_component(_installer "${STEMTEX_INSTALLER}" ABSOLUTE)
        if(NOT EXISTS "${_installer}")
            message(FATAL_ERROR "STEMTEX_INSTALLER does not exist: ${_installer}")
        endif()
    else()
        file(MAKE_DIRECTORY "${WORKSPACE_ROOT}/downloads")
        if(NOT EXISTS "${_installer}")
            message(STATUS "[run ] downloading ${_installer_name}")
            if(STEMTEX_INSTALLER_SHA256 STREQUAL "")
                file(DOWNLOAD "${STEMTEX_DOWNLOAD_BASE}/${_installer_name}" "${_installer}" SHOW_PROGRESS)
            else()
                file(DOWNLOAD "${STEMTEX_DOWNLOAD_BASE}/${_installer_name}" "${_installer}"
                     EXPECTED_HASH "SHA256=${STEMTEX_INSTALLER_SHA256}" SHOW_PROGRESS)
            endif()
        else()
            message(STATUS "[skip] ${_installer_name} is already in ${WORKSPACE_ROOT}/downloads")
        endif()
    endif()

    if(NOT EXISTS "${_installer}")
        message(FATAL_ERROR "Cannot obtain the StemTeX installer ${_installer_name}.")
    endif()
    if(NOT STEMTEX_INSTALLER_SHA256 STREQUAL "")
        file(SHA256 "${_installer}" _installer_sha256)
        string(TOLOWER "${STEMTEX_INSTALLER_SHA256}" _expected_lower)
        string(TOLOWER "${_installer_sha256}" _actual_lower)
        if(NOT _actual_lower STREQUAL _expected_lower)
            message(FATAL_ERROR
                "The StemTeX installer has checksum ${_installer_sha256}, expected ${STEMTEX_INSTALLER_SHA256}.")
        endif()
    endif()

    message(STATUS "[run ] installing StemTeX ${STEMTEX_VERSION} into ${STEMTEX_STAGE_ROOT}")
    file(MAKE_DIRECTORY "${WORKSPACE_ROOT}/logs")
    mengshee_remove_inside("${STEMTEX_STAGE_ROOT}" "${WORKSPACE_ROOT}/stemtex")
    execute_process(
        COMMAND "${_installer}"
                /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-
                "/TYPE=${STEMTEX_SETUP_TYPE}"
                "/DIR=${STEMTEX_STAGE_ROOT}"
                "/LOG=${_setup_log}"
        RESULT_VARIABLE _setup_result)
    if(NOT _setup_result EQUAL 0)
        message(FATAL_ERROR
            "The StemTeX installer failed with exit code ${_setup_result}; see ${_setup_log}.")
    endif()

    if(NOT EXISTS "${STEMTEX_STAGE_ROOT}/runtime/VERSION")
        message(FATAL_ERROR "The StemTeX installer did not create ${STEMTEX_STAGE_ROOT}/runtime/VERSION.")
    endif()
    file(READ "${STEMTEX_STAGE_ROOT}/runtime/VERSION" _installed_version)
    string(STRIP "${_installed_version}" _installed_version)
    if(NOT _installed_version STREQUAL STEMTEX_VERSION)
        message(FATAL_ERROR
            "The StemTeX installer produced runtime ${_installed_version}, expected ${STEMTEX_VERSION}.")
    endif()
endif()

# ---------------------------------------------------------------------------
# QScintilla headers and import library from the pinned StemTeX tag
# ---------------------------------------------------------------------------

set(_qscintilla_ready FALSE)
if(EXISTS "${_qscintilla_root}/third_party/qscintilla-src/src/Qsci/qsciscintilla.h"
   AND EXISTS "${_qscintilla_root}/third_party/qscintilla-src/scintilla/include/Scintilla.h"
   AND EXISTS "${_qscintilla_root}/third_party/qscintilla-build/release/qscintilla2_qt6.lib"
   AND EXISTS "${_qscintilla_root}/third_party/qscintilla-build/release/qscintilla2_qt6.dll")
    set(_qscintilla_ready TRUE)
endif()

if(_qscintilla_ready AND NOT (DEFINED FORCE AND FORCE))
    message(STATUS "[skip] QScintilla: ${_qscintilla_root}/third_party is already populated")
else()
    find_package(Git QUIET)
    if(NOT GIT_EXECUTABLE)
        find_program(GIT_EXECUTABLE NAMES git)
    endif()
    if(NOT GIT_EXECUTABLE)
        message(FATAL_ERROR "Git is required to fetch the pinned QScintilla files.")
    endif()

    message(STATUS "[run ] sparse checkout of ${STEMTEX_REPO} at ${STEMTEX_QSCINTILLA_REF}")
    mengshee_remove_inside("${_qscintilla_root}" "${WORKSPACE_ROOT}/stemtex")
    # Blob-less, single-branch clone: only the sparse paths are fetched, not the
    # rest of the StemTeX repository (engine sources, prebuilt trees, ...).
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" clone --filter=blob:none --no-checkout --depth 1
                --branch "${STEMTEX_QSCINTILLA_REF}" "${STEMTEX_REPO}" "${_qscintilla_root}"
        RESULT_VARIABLE _clone_result)
    if(NOT _clone_result EQUAL 0)
        message(FATAL_ERROR "git clone of ${STEMTEX_REPO} failed with exit code ${_clone_result}.")
    endif()
    foreach(_command IN ITEMS "sparse-checkout init --cone"
                              "sparse-checkout set third_party/qscintilla-src/src/Qsci third_party/qscintilla-src/scintilla/include third_party/qscintilla-build/release"
                              "checkout")
        separate_arguments(_arguments UNIX_COMMAND "${_command}")
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_qscintilla_root}" ${_arguments}
                        RESULT_VARIABLE _git_result)
        if(NOT _git_result EQUAL 0)
            message(FATAL_ERROR "git ${_command} failed with exit code ${_git_result}.")
        endif()
    endforeach()

    foreach(_required IN ITEMS
            "third_party/qscintilla-src/src/Qsci/qsciscintilla.h"
            "third_party/qscintilla-src/scintilla/include/Scintilla.h"
            "third_party/qscintilla-build/release/qscintilla2_qt6.lib"
            "third_party/qscintilla-build/release/qscintilla2_qt6.dll")
        if(NOT EXISTS "${_qscintilla_root}/${_required}")
            message(FATAL_ERROR "QScintilla checkout is incomplete, missing ${_required}.")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# final validation: what the application build and the runtime deployment need
# ---------------------------------------------------------------------------

foreach(_required IN ITEMS
        "runtime/VERSION"
        "runtime/bin/sdk/stemtex-renderer.dll"
        "runtime/bin/sdk/stemtex-profile.dll"
        "runtime/bin/windows/stemtex-worker-host.exe"
        "runtime/bin/windows/xetexdaemon.exe"
        "runtime/bin/windows/xdvipdfmxdaemon.exe"
        "runtime/bin/windows/dvipdfmxdaemon.dll"
        "runtime/bin/windows/dvisvgmdaemon.exe"
        "runtime/bin/windows/dvisvgmdaemon.dll"
        "gui/stemtex-profile-creator.exe"
        "gui/profiles/unicodemath/preamble.tex"
        "runtime/texmf-dist/web2c/texmf.cnf")
    if(NOT EXISTS "${STEMTEX_STAGE_ROOT}/${_required}")
        message(FATAL_ERROR "The StemTeX runtime is incomplete, missing ${_required} under ${STEMTEX_STAGE_ROOT}.")
    endif()
endforeach()

message(STATUS "StemTeX runtime ready")
message(STATUS "  Stage     : ${STEMTEX_STAGE_ROOT}")
message(STATUS "  QScintilla: ${_qscintilla_root}/third_party")
