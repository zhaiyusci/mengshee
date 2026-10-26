cmake_minimum_required(VERSION 3.20)

# Prepares the StemTeX runtime that Mengshee renders LaTeX notes with.
#
# Normally nothing is installed: an existing StemTeX installation is used in
# place (see windows-build/cmake/stemtex-common.cmake for the resolution order)
# and only the QScintilla files the LaTeX note editor compiles against are
# fetched, from the StemTeX tag matching that runtime:
#
#   <WORKSPACE_ROOT>/stemtex/qscintilla/third_party/
#       qscintilla-src/src/Qsci, qscintilla-src/scintilla/include
#       qscintilla-build/release/qscintilla2_qt6.{lib,dll}
#
# When no StemTeX runtime is available the pinned release installer is
# downloaded (SHA-256 checked) and installed into
# <WORKSPACE_ROOT>/stemtex/StemTeX-<version>. That needs administrator rights
# once; -DSTEMTEX_INSTALLER=<exe> uses a local setup instead.
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

if(NOT DEFINED STEMTEX_REPO OR "${STEMTEX_REPO}" STREQUAL "")
    set(STEMTEX_REPO "https://github.com/zhaiyusci/stemtex.git")
endif()
if(NOT DEFINED STEMTEX_SETUP_TYPE OR "${STEMTEX_SETUP_TYPE}" STREQUAL "")
    # full = runtime + gui + bundled texmf; the packaging step needs all three.
    set(STEMTEX_SETUP_TYPE "full")
endif()

set(_qscintilla_root "${WORKSPACE_ROOT}/stemtex/qscintilla")
set(_qscintilla_marker "${_qscintilla_root}/.stemtex-ref")

message(STATUS "Mengshee StemTeX runtime")

# ---------------------------------------------------------------------------
# 1. the runtime: use what is installed, install only when nothing is there
# ---------------------------------------------------------------------------

mengshee_resolve_stemtex_runtime(STEMTEX_STAGE_ROOT STEMTEX_VERSION_RESOLVED "${WORKSPACE_ROOT}")

if(STEMTEX_STAGE_ROOT)
    message(STATUS "[skip] StemTeX runtime: using the installed StemTeX ${STEMTEX_VERSION_RESOLVED}")
    message(STATUS "       ${STEMTEX_STAGE_ROOT} (not copied, not rebuilt)")
    if(DEFINED STEMTEX_VERSION AND NOT "${STEMTEX_VERSION}" STREQUAL "" AND NOT STEMTEX_VERSION STREQUAL STEMTEX_VERSION_RESOLVED)
        message(FATAL_ERROR "Requested StemTeX ${STEMTEX_VERSION} but ${STEMTEX_STAGE_ROOT} is ${STEMTEX_VERSION_RESOLVED}.")
    endif()
    mengshee_validate_stemtex_runtime("${STEMTEX_STAGE_ROOT}")
else()
    if(NOT DEFINED STEMTEX_VERSION OR "${STEMTEX_VERSION}" STREQUAL "")
        set(STEMTEX_VERSION "${MENGSHEE_STEMTEX_DEFAULT_RELEASE}")
    endif()
    set(STEMTEX_VERSION_RESOLVED "${STEMTEX_VERSION}")
    set(STEMTEX_STAGE_ROOT "${WORKSPACE_ROOT}/stemtex/StemTeX-${STEMTEX_VERSION}")
    get_filename_component(STEMTEX_STAGE_ROOT "${STEMTEX_STAGE_ROOT}" ABSOLUTE)
    message(STATUS "No StemTeX runtime found; installing release ${STEMTEX_VERSION}")

    if(NOT DEFINED STEMTEX_DOWNLOAD_BASE OR "${STEMTEX_DOWNLOAD_BASE}" STREQUAL "")
        set(STEMTEX_DOWNLOAD_BASE "https://github.com/zhaiyusci/stemtex/releases/download/v${STEMTEX_VERSION}")
    endif()
    if(NOT DEFINED STEMTEX_INSTALLER_SHA256 OR "${STEMTEX_INSTALLER_SHA256}" STREQUAL "")
        # SHA-256 of StemTeX-<version>-Setup.exe, published with the release.
        if(STEMTEX_VERSION STREQUAL "0.15.0")
            set(STEMTEX_INSTALLER_SHA256 "04132c27d6427f24dd30f9d26b922e43047963c1a5ccf9e71bdb6d7d35912a8f")
        endif()
    endif()

    # The published setup requires administrator rights; say so before the UAC
    # machinery can fail with an unhelpful error.
    execute_process(COMMAND whoami /groups OUTPUT_VARIABLE _token_groups ERROR_QUIET RESULT_VARIABLE _token_result)
    if(NOT (_token_result EQUAL 0 AND _token_groups MATCHES "S-1-16-12288|S-1-16-16384"))
        message(FATAL_ERROR
            "No StemTeX runtime was found and installing StemTeX ${STEMTEX_VERSION} needs administrator rights.\n"
            "Choose one:\n"
            "  1. install StemTeX ${STEMTEX_VERSION} (the published setup) and run this driver again - it is then used in place,\n"
            "  2. run this driver once from an elevated PowerShell to install it into the workspace,\n"
            "  3. pass -DSTEMTEX_INSTALLER=<StemTeX-${STEMTEX_VERSION}-Setup.exe> while elevated, or\n"
            "  4. pass -DSTEMTEX_STAGE_ROOT=<directory> pointing at an existing StemTeX runtime.")
    endif()

    if(DEFINED STEMTEX_INSTALLER AND NOT "${STEMTEX_INSTALLER}" STREQUAL "")
        get_filename_component(_installer "${STEMTEX_INSTALLER}" ABSOLUTE)
        if(NOT EXISTS "${_installer}")
            message(FATAL_ERROR "STEMTEX_INSTALLER does not exist: ${_installer}")
        endif()
    else()
        set(_installer_name "StemTeX-${STEMTEX_VERSION}-Setup.exe")
        set(_installer "${WORKSPACE_ROOT}/downloads/${_installer_name}")
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
        message(FATAL_ERROR "Cannot obtain the StemTeX installer for ${STEMTEX_VERSION}.")
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
                "/LOG=${WORKSPACE_ROOT}/logs/stemtex-setup.log"
        RESULT_VARIABLE _setup_result)
    if(NOT _setup_result EQUAL 0)
        message(FATAL_ERROR
            "The StemTeX installer failed with exit code ${_setup_result}; see ${WORKSPACE_ROOT}/logs/stemtex-setup.log.")
    endif()
    mengshee_stemtex_version_of(_installed_version "${STEMTEX_STAGE_ROOT}")
    if(NOT _installed_version STREQUAL STEMTEX_VERSION)
        message(FATAL_ERROR
            "The StemTeX installer produced runtime ${_installed_version}, expected ${STEMTEX_VERSION}.")
    endif()
    mengshee_validate_stemtex_runtime("${STEMTEX_STAGE_ROOT}")
endif()

# ---------------------------------------------------------------------------
# 2. QScintilla headers and import library, from the runtime's own tag
# ---------------------------------------------------------------------------

if(NOT DEFINED STEMTEX_QSCINTILLA_REF OR "${STEMTEX_QSCINTILLA_REF}" STREQUAL "")
    # The headers and the import library must match the DLL of this runtime.
    set(STEMTEX_QSCINTILLA_REF "v${STEMTEX_VERSION_RESOLVED}")
endif()

set(_qscintilla_ready FALSE)
if(EXISTS "${_qscintilla_root}/third_party/qscintilla-src/src/Qsci/qsciscintilla.h"
   AND EXISTS "${_qscintilla_root}/third_party/qscintilla-src/scintilla/include/Scintilla.h"
   AND EXISTS "${_qscintilla_root}/third_party/qscintilla-build/release/qscintilla2_qt6.lib"
   AND EXISTS "${_qscintilla_root}/third_party/qscintilla-build/release/qscintilla2_qt6.dll"
   AND EXISTS "${_qscintilla_marker}")
    file(READ "${_qscintilla_marker}" _fetched_ref)
    string(STRIP "${_fetched_ref}" _fetched_ref)
    if(_fetched_ref STREQUAL STEMTEX_QSCINTILLA_REF)
        set(_qscintilla_ready TRUE)
    else()
        message(STATUS "QScintilla was fetched for ${_fetched_ref}, this runtime needs ${STEMTEX_QSCINTILLA_REF}; refetching")
    endif()
endif()

if(_qscintilla_ready AND NOT (DEFINED FORCE AND FORCE))
    message(STATUS "[skip] QScintilla: ${_qscintilla_root}/third_party is already at ${STEMTEX_QSCINTILLA_REF}")
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
    file(WRITE "${_qscintilla_marker}" "${STEMTEX_QSCINTILLA_REF}\n")
endif()

message(STATUS "StemTeX runtime ready")
message(STATUS "  Runtime   : ${STEMTEX_STAGE_ROOT} (${STEMTEX_VERSION_RESOLVED})")
message(STATUS "  QScintilla: ${_qscintilla_root}/third_party (${STEMTEX_QSCINTILLA_REF})")
