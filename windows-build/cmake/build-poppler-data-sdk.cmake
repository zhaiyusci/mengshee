cmake_minimum_required(VERSION 3.20)

# Installs the Poppler CMap / CID unicode data (the separate upstream
# poppler-data release) into the SDK prefix.
#
# Neither the custom Poppler build nor package-windows.cmake provides this data,
# but the staged runtime must contain share/poppler/cMap and share/poppler/
# cidToUnicode: package-windows.cmake validates share/poppler/cMap/Adobe-GB1/
# UniGB-UTF16-H and share/poppler/cidToUnicode/Adobe-GB1, and PDFs that rely on
# CJK or other encoded fonts need them at run time.
#
# The tarball is a compiler-less CMake project, so it installs with the same
# CMake that drives the rest of the build.
#
# Example:
#   & "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
#     -DWORKSPACE_ROOT=C:/path/to/checkout/windows-build-workspace `
#     -P windows-build/cmake/build-poppler-data-sdk.cmake

get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_windows_build_dir "${_script_dir}/.." ABSOLUTE)
get_filename_component(_default_source_root "${_windows_build_dir}/.." ABSOLUTE)
get_filename_component(_default_workspace_root "${_default_source_root}/windows-build-workspace" ABSOLUTE)
include("${_script_dir}/kf6-sdk-common.cmake")

if(NOT DEFINED POPPLER_DATA_VERSION OR "${POPPLER_DATA_VERSION}" STREQUAL "")
    set(POPPLER_DATA_VERSION "0.4.12")
endif()

mengshee_default_path(SOURCE_ROOT "${_default_source_root}")
mengshee_default_path(WORKSPACE_ROOT "${_default_workspace_root}")
mengshee_default_path(SDK_PREFIX "${WORKSPACE_ROOT}/sdk")
mengshee_find_toolchain()

set(_url "https://poppler.freedesktop.org/poppler-data-${POPPLER_DATA_VERSION}.tar.gz")
set(_sha256 "c835b640a40ce357e1b83666aabd95edffa24ddddd49b8daff63adb851cdab74")
set(_archive "${WORKSPACE_ROOT}/downloads/poppler-data-${POPPLER_DATA_VERSION}.tar.gz")
set(_source "${WORKSPACE_ROOT}/build/thirdparty/poppler-data-${POPPLER_DATA_VERSION}")
set(_build "${WORKSPACE_ROOT}/build/thirdparty/poppler-data-build")
set(_marker_file "${SDK_PREFIX}/share/poppler/cMap/Adobe-GB1/UniGB-UTF16-H")

message(STATUS "Mengshee Poppler data install")
message(STATUS "  Version   : ${POPPLER_DATA_VERSION}")
message(STATUS "  URL       : ${_url}")
message(STATUS "  SdkPrefix : ${SDK_PREFIX}")

if(EXISTS "${_marker_file}" AND NOT (DEFINED FORCE AND FORCE))
    message(STATUS "poppler-data is already installed in ${SDK_PREFIX}; nothing to do.")
    return()
endif()

mengshee_remove_inside("${_source}" "${WORKSPACE_ROOT}/build")
file(MAKE_DIRECTORY "${WORKSPACE_ROOT}/downloads" "${WORKSPACE_ROOT}/build/thirdparty")

message(STATUS "Downloading ${_url}")
file(DOWNLOAD "${_url}" "${_archive}" SHOW_PROGRESS)
file(SHA256 "${_archive}" _actual_sha256)
string(TOLOWER "${_sha256}" _expected_lower)
string(TOLOWER "${_actual_sha256}" _actual_lower)
if(NOT _actual_lower STREQUAL _expected_lower)
    message(STATUS "Checksum mismatch for ${_archive}; downloading again.")
    file(REMOVE "${_archive}")
    file(DOWNLOAD "${_url}" "${_archive}" EXPECTED_HASH "SHA256=${_sha256}" SHOW_PROGRESS)
    file(SHA256 "${_archive}" _actual_sha256)
    string(TOLOWER "${_actual_sha256}" _actual_lower)
    if(NOT _actual_lower STREQUAL _expected_lower)
        message(FATAL_ERROR "Downloaded poppler-data has checksum ${_actual_sha256}, expected ${_sha256}.")
    endif()
endif()

file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${WORKSPACE_ROOT}/build/thirdparty")
if(NOT EXISTS "${_source}/CMakeLists.txt")
    message(FATAL_ERROR "Extracted poppler-data does not contain CMakeLists.txt: ${_source}")
endif()

# project(... NONE): configure and install only, no compiler involved. Ninja is
# named explicitly because the default generator on Windows may be NMake.
mengshee_remove_inside("${_build}" "${WORKSPACE_ROOT}/build/thirdparty")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_build}" -G Ninja
            "-DCMAKE_MAKE_PROGRAM=${NINJA_PROGRAM}"
            "-DCMAKE_INSTALL_PREFIX=${SDK_PREFIX}"
    RESULT_VARIABLE _configure_result)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "Configuring poppler-data failed with exit code ${_configure_result}.")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${_build}"
    RESULT_VARIABLE _install_result)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "Installing poppler-data failed with exit code ${_install_result}.")
endif()

foreach(_required IN ITEMS
        "share/poppler/cMap/Adobe-GB1/UniGB-UTF16-H"
        "share/poppler/cidToUnicode/Adobe-GB1"
        "share/poppler/nameToUnicode"
        "share/poppler/unicodeMap")
    if(NOT EXISTS "${SDK_PREFIX}/${_required}")
        message(FATAL_ERROR "poppler-data install is incomplete, missing ${_required} under ${SDK_PREFIX}.")
    endif()
endforeach()

message(STATUS "poppler-data ${POPPLER_DATA_VERSION} installed into ${SDK_PREFIX}/share/poppler.")
