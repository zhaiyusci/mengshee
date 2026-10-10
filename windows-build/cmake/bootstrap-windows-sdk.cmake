cmake_minimum_required(VERSION 3.20)

# One-command bootstrap of the whole Windows SDK prefix, in dependency order:
#
#   python       workspace venv with lxml (breeze-icons icon generation)
#   ecm          extra-cmake-modules
#   gettext      native Windows msgfmt
#   winflexbison pinned flex/bison plus their data directory
#   zlib         zlib
#   freetype     freetype
#   libintl      libintl compatibility shim used by KI18n
#   kf6          every KF6 module listed in manifests/kf6-modules.txt
#   poppler      the pinned Poppler fork in external/poppler + Base14 fonts
#   poppler-data Poppler CMap/CID data required by the staged runtime
#   tesseract    Tesseract/Leptonica OCR runtime with the English model
#
# Each step is skipped when its result is already present, so the script is
# cheap to re-run; -DFORCE=ON redoes everything and -DSTEPS=<list> runs a
# subset (for example -DSTEPS=poppler-data).
#
# Example:
#   & "C:\Qt\Tools\CMake_64\bin\cmake.exe" `
#     -DQT_PREFIX=C:/Qt/6.11.1/msvc2022_64 `
#     -DVCVARS="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" `
#     -DWORKSPACE_ROOT=C:/path/to/checkout/windows_build `
#     -P windows-build/cmake/bootstrap-windows-sdk.cmake

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
if(NOT DEFINED KF6_VERSION OR "${KF6_VERSION}" STREQUAL "")
    set(KF6_VERSION "6.27.0")
endif()
if(NOT DEFINED BUILD_TYPE OR "${BUILD_TYPE}" STREQUAL "")
    set(BUILD_TYPE "RelWithDebInfo")
endif()

set(_all_steps python ecm gettext winflexbison zlib freetype libintl kf6 poppler poppler-data tesseract)
if(DEFINED STEPS AND NOT "${STEPS}" STREQUAL "")
    set(_steps "${STEPS}")
else()
    set(_steps "${_all_steps}")
endif()

set(_module_manifest "${_windows_build_dir}/manifests/kf6-modules.txt")
if(NOT EXISTS "${_module_manifest}")
    message(FATAL_ERROR "Missing KF6 module manifest: ${_module_manifest}")
endif()

message(STATUS "Mengshee Windows SDK bootstrap")
message(STATUS "  Source    : ${SOURCE_ROOT}")
message(STATUS "  Workspace : ${WORKSPACE_ROOT}")
message(STATUS "  SdkPrefix : ${SDK_PREFIX}")
message(STATUS "  QtPrefix  : ${QT_PREFIX}")
message(STATUS "  KF6       : ${KF6_VERSION}")
message(STATUS "  Steps     : ${_steps}")
if(DEFINED FORCE AND FORCE)
    message(STATUS "  Force     : ON (every step runs again)")
endif()

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

function(mengshee_step_enabled out_var step)
    if("${_steps}" STREQUAL "")
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()
    if(_steps MATCHES "(^|;)${step}(;|$)")
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(mengshee_sdk_step_required out_var step marker)
    mengshee_step_enabled(_enabled "${step}")
    if(NOT _enabled)
        set(${out_var} FALSE PARENT_SCOPE)
        return()
    endif()
    if(DEFINED FORCE AND FORCE)
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()
    if(EXISTS "${marker}")
        message(STATUS "[skip] ${step}: ${marker} already exists")
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

# Runs another windows-build/cmake script with the shared arguments.
function(mengshee_run_sdk_script script_name)
    set(_args
        "-DQT_PREFIX=${QT_PREFIX}"
        "-DVCVARS=${VCVARS}"
        "-DSOURCE_ROOT=${SOURCE_ROOT}"
        "-DWORKSPACE_ROOT=${WORKSPACE_ROOT}"
        "-DSDK_PREFIX=${SDK_PREFIX}"
        "-DBUILD_TYPE=${BUILD_TYPE}"
        "-DJOBS=${JOBS}"
        "-DKF6_VERSION=${KF6_VERSION}"
        "-DCMAKE_EXE=${CMAKE_PROGRAM}"
        "-DNINJA=${NINJA_PROGRAM}"
    )
    if(DEFINED FORCE AND FORCE)
        list(APPEND _args "-DFORCE=ON" "-DCLEAN=ON")
    endif()
    list(APPEND _args ${ARGN})
    message(STATUS "[run ] ${script_name}")
    execute_process(COMMAND "${CMAKE_PROGRAM}" ${_args} -P "${_script_dir}/${script_name}"
                    RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${script_name} failed with exit code ${_result}.")
    endif()
endfunction()

# ---------------------------------------------------------------------------
# python: a workspace venv that can run breeze-icons' icon generation script
# ---------------------------------------------------------------------------

function(mengshee_find_base_python out_var)
    set(_candidates)
    if(DEFINED PYTHON_EXECUTABLE AND NOT "${PYTHON_EXECUTABLE}" STREQUAL "")
        list(APPEND _candidates "${PYTHON_EXECUTABLE}")
    endif()
    if(DEFINED ENV{MENGSHEE_PYTHON_EXECUTABLE})
        list(APPEND _candidates "$ENV{MENGSHEE_PYTHON_EXECUTABLE}")
    endif()
    # Store/nuget style installs keep their real interpreter outside the
    # WindowsApps alias that shadows `python` on PATH.
    file(GLOB _local_pythons LIST_DIRECTORIES FALSE
        "$ENV{LOCALAPPDATA}/Python/pythoncore-*/python.exe"
        "$ENV{LOCALAPPDATA}/Programs/Python/Python3*/python.exe")
    list(APPEND _candidates ${_local_pythons})
    if(DEFINED Python3_EXECUTABLE AND NOT "${Python3_EXECUTABLE}" STREQUAL "")
        list(APPEND _candidates "${Python3_EXECUTABLE}")
    endif()
    unset(_path_python CACHE)
    find_program(_path_python NAMES python python3)
    if(_path_python)
        list(APPEND _candidates "${_path_python}")
    endif()

    foreach(_candidate IN LISTS _candidates)
        if(NOT EXISTS "${_candidate}")
            continue()
        endif()
        execute_process(COMMAND "${_candidate}" -c "import sys, venv; print(sys.version)"
                        RESULT_VARIABLE _probe_result OUTPUT_QUIET ERROR_QUIET)
        if(_probe_result EQUAL 0)
            get_filename_component(_python "${_candidate}" ABSOLUTE)
            message(STATUS "Using Python: ${_python}")
            set(${out_var} "${_python}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR
        "Cannot find a usable Python 3 interpreter. Pass -DPYTHON_EXECUTABLE=<python.exe>.\n"
        "A Store alias for python.exe is not enough; the module venv support is required.")
endfunction()

function(mengshee_ensure_python_venv out_var)
    mengshee_step_enabled(_enabled python)
    if(NOT _enabled)
        # Later steps still need the interpreter path.
        set(${out_var} "${WORKSPACE_ROOT}/tools/pyvenv/Scripts/python.exe" PARENT_SCOPE)
        return()
    endif()

    set(_venv "${WORKSPACE_ROOT}/tools/pyvenv")
    if(DEFINED FORCE AND FORCE)
        mengshee_remove_inside("${_venv}" "${WORKSPACE_ROOT}/tools")
    endif()
    if(NOT EXISTS "${_venv}/Scripts/python.exe")
        mengshee_find_base_python(_base_python)
        message(STATUS "[run ] python: creating ${_venv}")
        execute_process(COMMAND "${_base_python}" -m venv "${_venv}" RESULT_VARIABLE _venv_result)
        if(NOT _venv_result EQUAL 0)
            message(FATAL_ERROR "Creating the workspace Python venv failed with exit code ${_venv_result}.")
        endif()
    else()
        message(STATUS "[skip] python: ${_venv} already exists")
    endif()

    execute_process(COMMAND "${_venv}/Scripts/python.exe" -c "import lxml" RESULT_VARIABLE _lxml_result OUTPUT_QUIET ERROR_QUIET)
    if(NOT _lxml_result EQUAL 0)
        message(STATUS "[run ] python: installing lxml into ${_venv}")
        execute_process(COMMAND "${_venv}/Scripts/python.exe" -m pip install --disable-pip-version-check --quiet lxml
                        RESULT_VARIABLE _pip_result)
        if(NOT _pip_result EQUAL 0)
            message(FATAL_ERROR "Installing lxml into ${_venv} failed with exit code ${_pip_result}.")
        endif()
    else()
        message(STATUS "[skip] python: lxml is already available")
    endif()
    set(${out_var} "${_venv}/Scripts/python.exe" PARENT_SCOPE)
endfunction()

mengshee_ensure_python_venv(MENGSHEE_PYTHON)

# ---------------------------------------------------------------------------
# ECM and the small tool downloads
# ---------------------------------------------------------------------------

mengshee_sdk_step_required(_run ecm "${SDK_PREFIX}/share/ECM/cmake/ECMConfig.cmake")
if(_run)
    mengshee_run_sdk_script(bootstrap-kf6-sdk.cmake)
endif()

mengshee_sdk_step_required(_run gettext "${SDK_PREFIX}/tools/gettext-native/bin/msgfmt.exe")
if(_run)
    mengshee_run_sdk_script(install-gettext-native-sdk.cmake)
endif()

# bison needs its m4sugar/skeleton data next to the executable; the marker is
# that data, not just the executables.
mengshee_sdk_step_required(_run winflexbison "${SDK_PREFIX}/bin/data/m4sugar/m4sugar.m4")
if(_run)
    mengshee_run_sdk_script(install-winflexbison-sdk.cmake)
endif()

# ---------------------------------------------------------------------------
# support libraries
# ---------------------------------------------------------------------------

mengshee_sdk_step_required(_run zlib "${SDK_PREFIX}/lib/zlib.lib")
if(_run)
    mengshee_run_sdk_script(build-zlib-sdk.cmake)
endif()

mengshee_sdk_step_required(_run freetype "${SDK_PREFIX}/include/freetype2/freetype/freetype.h")
if(_run)
    mengshee_run_sdk_script(build-freetype-sdk.cmake)
endif()

mengshee_sdk_step_required(_run libintl "${SDK_PREFIX}/bin/intl.dll")
if(_run)
    mengshee_run_sdk_script(build-libintl-shim-sdk.cmake)
endif()

mengshee_find_msgfmt(MSGFMT "${SDK_PREFIX}")

# ---------------------------------------------------------------------------
# KF6 modules
# ---------------------------------------------------------------------------

mengshee_step_enabled(_kf6_enabled kf6)
if(_kf6_enabled)
    set(_module_records "${SDK_PREFIX}/mengshee-sdk-modules.cmake")
    if(EXISTS "${_module_records}")
        file(READ "${_module_records}" _records_content)
    else()
        set(_records_content "")
    endif()

    file(STRINGS "${_module_manifest}" _manifest_lines)
    foreach(_line IN LISTS _manifest_lines)
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^#")
            continue()
        endif()
        separate_arguments(_parts UNIX_COMMAND "${_line}")
        list(GET _parts 0 _module)
        list(REMOVE_AT _parts 0)

        set(_url "")
        set(_ref "")
        set(_extra "-DPython3_EXECUTABLE=${MENGSHEE_PYTHON};-DPython_EXECUTABLE=${MENGSHEE_PYTHON}")
        foreach(_option IN LISTS _parts)
            if(_option MATCHES "^GIT_URL=")
                string(SUBSTRING "${_option}" 8 -1 _url)
            elseif(_option MATCHES "^GIT_REF=")
                string(SUBSTRING "${_option}" 8 -1 _ref)
            elseif(_option MATCHES "^EXTRA=")
                string(SUBSTRING "${_option}" 6 -1 _extra_options)
                string(REPLACE "," ";" _extra_options "${_extra_options}")
                list(APPEND _extra ${_extra_options})
            else()
                message(FATAL_ERROR "Unknown option '${_option}' for KF6 module ${_module} in ${_module_manifest}")
            endif()
        endforeach()

        set(_step_name "kf6-${_module}")
        if(NOT (DEFINED FORCE AND FORCE))
            string(FIND "${_records_content}" "MENGSHEE_KF6_MODULE_${_module}_GIT_REF" _recorded)
            if(NOT _recorded EQUAL -1)
                message(STATUS "[skip] ${_step_name}: already recorded in ${_module_records}")
                continue()
            endif()
        endif()

        set(_module_args "-DMODULE=${_module}")
        if(NOT _url STREQUAL "")
            list(APPEND _module_args "-DGIT_URL=${_url}")
        endif()
        if(NOT _ref STREQUAL "")
            list(APPEND _module_args "-DGIT_REF=${_ref}")
        endif()
        string(REPLACE ";" "," _extra_joined "${_extra}")
        list(APPEND _module_args "-DEXTRA_CMAKE_ARGS=${_extra_joined}")

        message(STATUS "[run ] ${_step_name}")
        execute_process(COMMAND "${CMAKE_PROGRAM}"
            "-DQT_PREFIX=${QT_PREFIX}"
            "-DVCVARS=${VCVARS}"
            "-DSOURCE_ROOT=${SOURCE_ROOT}"
            "-DWORKSPACE_ROOT=${WORKSPACE_ROOT}"
            "-DSDK_PREFIX=${SDK_PREFIX}"
            "-DBUILD_TYPE=${BUILD_TYPE}"
            "-DJOBS=${JOBS}"
            "-DKF6_VERSION=${KF6_VERSION}"
            "-DCMAKE_EXE=${CMAKE_PROGRAM}"
            "-DNINJA=${NINJA_PROGRAM}"
            ${_module_args}
            -P "${_script_dir}/build-kf6-module.cmake"
            RESULT_VARIABLE _module_result)
        if(NOT _module_result EQUAL 0)
            message(FATAL_ERROR "Building KF6 module ${_module} failed with exit code ${_module_result}.")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Poppler, Poppler data, Tesseract
# ---------------------------------------------------------------------------

mengshee_sdk_step_required(_run poppler "${SDK_PREFIX}/bin/poppler-qt6.dll")
if(_run)
    mengshee_run_sdk_script(build-poppler-sdk.cmake)
endif()

mengshee_sdk_step_required(_run poppler-data "${SDK_PREFIX}/share/poppler/cMap/Adobe-GB1/UniGB-UTF16-H")
if(_run)
    mengshee_run_sdk_script(build-poppler-data-sdk.cmake)
endif()

mengshee_sdk_step_required(_run tesseract "${SDK_PREFIX}/bin/tesseract55.dll")
if(_run)
    mengshee_run_sdk_script(build-tesseract-sdk.cmake)
endif()

message(STATUS "Windows SDK bootstrap complete: ${SDK_PREFIX}")
