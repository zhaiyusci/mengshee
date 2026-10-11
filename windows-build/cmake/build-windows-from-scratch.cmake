cmake_minimum_required(VERSION 3.20)

# Complete Windows build from a fresh checkout, driven by CMake only:
#
#   sdk      windows-build/cmake/bootstrap-windows-sdk.cmake
#   stemtex  windows-build/cmake/build-stemtex-stage.cmake
#   app      windows-build/cmake/build-windows.cmake (build, deploy, package)
#   smoke    windows-build/scripts/smoke-test-mengshee-stage.ps1
#
# Unlike the older single-purpose drivers, the workspace defaults to
# <SOURCE_ROOT>/windows-build-workspace so a checkout keeps its builds inside
# itself. The name is deliberately different from windows-build/, which holds
# the build scripts themselves:
#
#   windows-build-workspace/sdk                            SDK prefix
#   windows-build-workspace/sources, build, downloads      sources, build trees, caches
#   windows-build-workspace/stemtex/StemTeX-<version>      StemTeX stage
#   windows-build-workspace/install/mengshee               installed application tree
#   windows-build-workspace/dist/mengshee-pdf/app          staged runtime
#   windows-build-workspace/dist/installer                 installer output
#
# Pass -DWORKSPACE_ROOT=<dir> to keep the workspace somewhere else (the older
# sibling layout, for example ..\windows_build, still works), -DSTEPS=<list> to
# run a subset and -DFORCE=ON to redo the selected steps from scratch.
#
# Example:
#   & "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
#     -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
#     -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
#     -P windows-build/cmake/build-windows-from-scratch.cmake

get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_windows_build_dir "${_script_dir}/.." ABSOLUTE)
get_filename_component(_default_source_root "${_windows_build_dir}/.." ABSOLUTE)
include("${_script_dir}/kf6-sdk-common.cmake")

mengshee_default_path(SOURCE_ROOT "${_default_source_root}")
mengshee_default_path(WORKSPACE_ROOT "${SOURCE_ROOT}/windows-build-workspace")
mengshee_default_path(SDK_PREFIX "${WORKSPACE_ROOT}/sdk")
mengshee_default_path(INSTALL_PREFIX "${WORKSPACE_ROOT}/install/mengshee")
mengshee_default_path(BUILD_DIR "${WORKSPACE_ROOT}/build/mengshee-standalone")
mengshee_default_path(STAGE_ROOT "${WORKSPACE_ROOT}/dist/mengshee-pdf/app")
mengshee_default_path(OUTPUT_DIR "${WORKSPACE_ROOT}/dist/installer")
mengshee_find_toolchain()

if(NOT DEFINED JOBS OR "${JOBS}" STREQUAL "")
    set(JOBS 8)
endif()

set(_all_steps sdk stemtex app smoke)
if(DEFINED STEPS AND NOT "${STEPS}" STREQUAL "")
    set(_steps "${STEPS}")
else()
    set(_steps "${_all_steps}")
endif()

string(TOLOWER "${SOURCE_ROOT}" _source_lower)
string(TOLOWER "${WORKSPACE_ROOT}" _workspace_lower)
string(FIND "${_workspace_lower}" "${_source_lower}" _workspace_inside_source)
if(_workspace_inside_source EQUAL 0)
    message(STATUS "Workspace is inside the checkout; make sure it is not tracked by git")
endif()

message(STATUS "Mengshee Windows build from scratch")
message(STATUS "  Source    : ${SOURCE_ROOT}")
message(STATUS "  Workspace : ${WORKSPACE_ROOT}")
message(STATUS "  Steps     : ${_steps}")
if(DEFINED FORCE AND FORCE)
    message(STATUS "  Force     : ON")
endif()

function(mengshee_want_step out_var step)
    if(_steps MATCHES "(^|;)${step}(;|$)")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# Shared arguments for the sub-drivers.
set(_common_args
    "-DSOURCE_ROOT=${SOURCE_ROOT}"
    "-DWORKSPACE_ROOT=${WORKSPACE_ROOT}"
    "-DSDK_PREFIX=${SDK_PREFIX}"
    "-DQT_PREFIX=${QT_PREFIX}"
    "-DVCVARS=${VCVARS}"
    "-DJOBS=${JOBS}"
    "-DCMAKE_EXE=${CMAKE_PROGRAM}"
    "-DNINJA=${NINJA_PROGRAM}"
)
foreach(_passthrough IN ITEMS PYTHON_EXECUTABLE KF6_VERSION BUILD_TYPE STEMTEX_JOBS STEMTEX_TEXLIVE_ROOT VS_INSTALL_DIR MENGSHEE_BASH)
    if(DEFINED ${_passthrough} AND NOT "${${_passthrough}}" STREQUAL "")
        list(APPEND _common_args "-D${_passthrough}=${${_passthrough}}")
    endif()
endforeach()
if(DEFINED FORCE AND FORCE)
    list(APPEND _common_args "-DFORCE=ON")
endif()

function(mengshee_run_driver script_name)
    message(STATUS ">>> ${script_name}")
    execute_process(COMMAND "${CMAKE_PROGRAM}" ${_common_args} ${ARGN}
                    -P "${_script_dir}/${script_name}"
                    RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${script_name} failed with exit code ${_result}.")
    endif()
endfunction()

mengshee_want_step(_want sdk)
if(_want)
    if(DEFINED STEPS AND NOT "${STEPS}" STREQUAL "")
        mengshee_run_driver(bootstrap-windows-sdk.cmake "-DSTEPS=${STEPS}")
    else()
        mengshee_run_driver(bootstrap-windows-sdk.cmake)
    endif()
endif()

mengshee_want_step(_want stemtex)
if(_want)
    mengshee_run_driver(build-stemtex-stage.cmake)
endif()

mengshee_want_step(_want app)
if(_want)
    set(_app_args
        "-DINSTALL_PREFIX=${INSTALL_PREFIX}"
        "-DBUILD_DIR=${BUILD_DIR}"
        "-DSTAGE_ROOT=${STAGE_ROOT}"
        "-DOUTPUT_DIR=${OUTPUT_DIR}"
    )
    if(DEFINED STEMTEX_ROOT AND NOT "${STEMTEX_ROOT}" STREQUAL "")
        list(APPEND _app_args "-DSTEMTEX_ROOT=${STEMTEX_ROOT}")
    endif()
    if(DEFINED STEMTEX_STAGE_ROOT AND NOT "${STEMTEX_STAGE_ROOT}" STREQUAL "")
        list(APPEND _app_args "-DSTEMTEX_STAGE_ROOT=${STEMTEX_STAGE_ROOT}")
    endif()
    if(DEFINED SKIP_INSTALLER AND SKIP_INSTALLER)
        list(APPEND _app_args "-DSKIP_INSTALLER=ON")
    endif()
    mengshee_run_driver(build-windows.cmake ${_app_args})
endif()

mengshee_want_step(_want smoke)
if(_want AND NOT (DEFINED SKIP_SMOKE AND SKIP_SMOKE))
    if(NOT DEFINED SMOKE_PDF OR "${SMOKE_PDF}" STREQUAL "")
        set(SMOKE_PDF "${SOURCE_ROOT}/autotests/data/file2.pdf")
    endif()
    if(NOT EXISTS "${SMOKE_PDF}")
        message(STATUS "[skip] smoke: ${SMOKE_PDF} does not exist")
    elseif(NOT EXISTS "${STAGE_ROOT}/bin/mengshee.exe")
        message(STATUS "[skip] smoke: ${STAGE_ROOT}/bin/mengshee.exe does not exist")
    else()
        message(STATUS "[run ] smoke test on ${STAGE_ROOT}")
        execute_process(COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${_windows_build_dir}/scripts/smoke-test-mengshee-stage.ps1"
            -StageRoot "${STAGE_ROOT}" -PdfPath "${SMOKE_PDF}"
            RESULT_VARIABLE _smoke_result)
        if(NOT _smoke_result EQUAL 0)
            message(FATAL_ERROR "The staged runtime smoke test failed with exit code ${_smoke_result}.")
        endif()
    endif()
endif()

message(STATUS "Mengshee Windows build complete.")
message(STATUS "  Stage     : ${STAGE_ROOT}")
message(STATUS "  Installer : ${OUTPUT_DIR}")
