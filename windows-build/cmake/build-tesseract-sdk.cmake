cmake_minimum_required(VERSION 3.20)

get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_windows_build_dir "${_script_dir}/.." ABSOLUTE)
get_filename_component(_default_source_root "${_windows_build_dir}/.." ABSOLUTE)
get_filename_component(_default_workspace_root "${_default_source_root}/../windows_build" ABSOLUTE)
include("${_script_dir}/kf6-sdk-common.cmake")

mengshee_default_path(SOURCE_ROOT "${_default_source_root}")
mengshee_default_path(WORKSPACE_ROOT "${_default_workspace_root}")
mengshee_default_path(SDK_PREFIX "${WORKSPACE_ROOT}/sdk")
mengshee_find_toolchain()

if(NOT DEFINED BUILD_TYPE OR "${BUILD_TYPE}" STREQUAL "")
    set(BUILD_TYPE "RelWithDebInfo")
endif()
if(NOT DEFINED JOBS OR "${JOBS}" STREQUAL "")
    set(JOBS 8)
endif()

set(LEPTONICA_VERSION "1.87.0")
set(TESSERACT_VERSION "5.5.2")
set(TESSDATA_FAST_COMMIT "87416418657359cb625c412a48b6e1d6d41c29bd")
set(ENG_TRAINEDDATA_SHA256 "7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2")

set(SOURCE_CACHE "${WORKSPACE_ROOT}/sources")
set(BUILD_ROOT "${WORKSPACE_ROOT}/build/thirdparty")
set(LEPTONICA_SOURCE "${SOURCE_CACHE}/leptonica")
set(TESSERACT_SOURCE "${SOURCE_CACHE}/tesseract")
set(LEPTONICA_BUILD "${BUILD_ROOT}/leptonica")
set(TESSERACT_BUILD "${BUILD_ROOT}/tesseract")

message(STATUS "Mengshee SDK English OCR build")
message(STATUS "  Leptonica : ${LEPTONICA_VERSION}")
message(STATUS "  Tesseract : ${TESSERACT_VERSION}")
message(STATUS "  SdkPrefix : ${SDK_PREFIX}")

mengshee_ensure_git_checkout("https://github.com/DanBloomberg/leptonica.git" "${LEPTONICA_VERSION}" "${LEPTONICA_SOURCE}")
mengshee_ensure_git_checkout("https://github.com/tesseract-ocr/tesseract.git" "${TESSERACT_VERSION}" "${TESSERACT_SOURCE}")

if(DEFINED CLEAN AND CLEAN)
    mengshee_remove_inside("${LEPTONICA_BUILD}" "${BUILD_ROOT}")
    mengshee_remove_inside("${TESSERACT_BUILD}" "${BUILD_ROOT}")
endif()

mengshee_prepare_build_dir("${LEPTONICA_BUILD}" "${BUILD_ROOT}" "${LEPTONICA_SOURCE}")
set(_leptonica_configure
    "\"${CMAKE_PROGRAM}\""
    -S "\"${LEPTONICA_SOURCE}\""
    -B "\"${LEPTONICA_BUILD}\""
    -G Ninja
    "-DCMAKE_MAKE_PROGRAM=\"${NINJA_PROGRAM}\""
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=\"${SDK_PREFIX}\""
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_PROG=OFF
    -DSW_BUILD=OFF
    -DENABLE_ZLIB=OFF
    -DENABLE_PNG=OFF
    -DENABLE_GIF=OFF
    -DENABLE_JPEG=OFF
    -DENABLE_TIFF=OFF
    -DENABLE_WEBP=OFF
    -DENABLE_OPENJPEG=OFF
)
list(JOIN _leptonica_configure " " _leptonica_configure_command)
mengshee_run_vs("${_leptonica_configure_command}")
mengshee_run_vs("\"${CMAKE_PROGRAM}\" --build \"${LEPTONICA_BUILD}\" --target install --parallel ${JOBS}")

mengshee_prepare_build_dir("${TESSERACT_BUILD}" "${BUILD_ROOT}" "${TESSERACT_SOURCE}")
set(_prefix_path "${SDK_PREFIX}")
set(_tesseract_configure
    "\"${CMAKE_PROGRAM}\""
    -S "\"${TESSERACT_SOURCE}\""
    -B "\"${TESSERACT_BUILD}\""
    -G Ninja
    "-DCMAKE_MAKE_PROGRAM=\"${NINJA_PROGRAM}\""
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_INSTALL_PREFIX=\"${SDK_PREFIX}\""
    "-DCMAKE_PREFIX_PATH=\"${_prefix_path}\""
    -DBUILD_SHARED_LIBS=ON
    -DBUILD_TRAINING_TOOLS=OFF
    -DBUILD_TESTS=OFF
    -DSW_BUILD=OFF
    -DOPENMP_BUILD=OFF
    -DGRAPHICS_DISABLED=ON
    -DDISABLED_LEGACY_ENGINE=OFF
    -DDISABLE_TIFF=ON
    -DDISABLE_ARCHIVE=ON
    -DDISABLE_CURL=ON
    -DINSTALL_CONFIGS=OFF
    -DENABLE_NATIVE=OFF
    -DENABLE_CCACHE=OFF
)
list(JOIN _tesseract_configure " " _tesseract_configure_command)
# Leptonica's exported CMake target omits its Windows import define.  Supply
# it through CXXFLAGS so CMake appends it to, rather than replaces, MSVC's
# standard compiler flags (/EHsc, /GR, and the Windows platform definitions).
set(_tesseract_configure_command "set \"CXXFLAGS=/DLIBLEPT_IMPORTS\" && ${_tesseract_configure_command}")
mengshee_run_vs("${_tesseract_configure_command}")
mengshee_run_vs("\"${CMAKE_PROGRAM}\" --build \"${TESSERACT_BUILD}\" --target install --parallel ${JOBS}")

set(_tessdata_dir "${SDK_PREFIX}/share/tessdata")
set(_english_model "${_tessdata_dir}/eng.traineddata")
file(MAKE_DIRECTORY "${_tessdata_dir}")
file(DOWNLOAD
    "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/${TESSDATA_FAST_COMMIT}/eng.traineddata"
    "${_english_model}"
    EXPECTED_HASH "SHA256=${ENG_TRAINEDDATA_SHA256}"
    TLS_VERIFY ON
    SHOW_PROGRESS
    STATUS _download_status
)
list(GET _download_status 0 _download_code)
if(NOT _download_code EQUAL 0)
    list(GET _download_status 1 _download_message)
    message(FATAL_ERROR "Could not download the English OCR model: ${_download_message}")
endif()

foreach(_required IN ITEMS
    "bin/leptonica-${LEPTONICA_VERSION}.dll"
    "bin/tesseract55.dll"
    "lib/leptonica-${LEPTONICA_VERSION}.lib"
    "lib/tesseract55.lib"
    "include/tesseract/baseapi.h"
    "share/tessdata/eng.traineddata"
)
    if(NOT EXISTS "${SDK_PREFIX}/${_required}")
        message(FATAL_ERROR "English OCR SDK is missing ${_required} in ${SDK_PREFIX}.")
    endif()
endforeach()

execute_process(
    COMMAND "${SDK_PREFIX}/bin/tesseract.exe"
            --list-langs
            --tessdata-dir "${_tessdata_dir}"
    RESULT_VARIABLE _tesseract_smoke_result
    OUTPUT_VARIABLE _tesseract_smoke_output
    ERROR_VARIABLE _tesseract_smoke_error
)
if(NOT _tesseract_smoke_result EQUAL 0 OR NOT _tesseract_smoke_output MATCHES "eng")
    message(FATAL_ERROR
        "The built Tesseract runtime failed its English initialization smoke test "
        "(exit ${_tesseract_smoke_result}).\n${_tesseract_smoke_output}${_tesseract_smoke_error}")
endif()

set(_tesseract_smoke_image "${BUILD_ROOT}/tesseract-english-smoke.pgm")
string(REPEAT "255 " 256 _tesseract_smoke_pixels)
file(WRITE "${_tesseract_smoke_image}" "P2\n16 16\n255\n${_tesseract_smoke_pixels}\n")
execute_process(
    COMMAND "${SDK_PREFIX}/bin/tesseract.exe"
            "${_tesseract_smoke_image}"
            stdout
            --tessdata-dir "${_tessdata_dir}"
            -l eng
            --oem 1
            --psm 6
    RESULT_VARIABLE _tesseract_recognition_result
    OUTPUT_VARIABLE _tesseract_recognition_output
    ERROR_VARIABLE _tesseract_recognition_error
)
file(REMOVE "${_tesseract_smoke_image}")
if(NOT _tesseract_recognition_result EQUAL 0)
    message(FATAL_ERROR
        "The built Tesseract runtime failed its English recognition smoke test "
        "(exit ${_tesseract_recognition_result}).\n${_tesseract_recognition_output}${_tesseract_recognition_error}")
endif()

message(STATUS "English OCR SDK installed into ${SDK_PREFIX}.")
