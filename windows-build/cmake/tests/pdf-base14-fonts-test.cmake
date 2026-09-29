# Isolated file-only deployment tests; does not configure or build any target.
cmake_minimum_required(VERSION 3.21)
get_filename_component(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/../pdf-base14-fonts.cmake")

if(DEFINED VALIDATE_PREFIX)
    mengshee_validate_pdf_base14_fonts("${SOURCE_ROOT}" "${VALIDATE_PREFIX}")
    return()
endif()

set(_scratch "${SOURCE_ROOT}/.pdf-base14-font-test")
if(EXISTS "${_scratch}")
    message(FATAL_ERROR "Refusing to overwrite existing test scratch directory: ${_scratch}")
endif()
set(_install "${_scratch}/install with spaces")
set(_stage "${_scratch}/relocated-stage")

mengshee_validate_pdf_base14_sources("${SOURCE_ROOT}")
mengshee_deploy_pdf_base14_fonts("${SOURCE_ROOT}" "${_install}")
mengshee_stage_pdf_base14_fonts("${SOURCE_ROOT}" "${_install}" "${_stage}")
mengshee_validate_pdf_base14_fonts("${SOURCE_ROOT}" "${_stage}")

file(WRITE "${_stage}/share/fonts/FoxitSymbol.cff" "deliberately corrupt test data")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVALIDATE_PREFIX=${_stage}" -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err
)
if(_result EQUAL 0 OR NOT "${_out}${_err}" MATCHES "checksum mismatch")
    message(FATAL_ERROR "Corrupt runtime font was not rejected with a checksum diagnostic: ${_out}${_err}")
endif()

file(REMOVE "${_stage}/share/fonts/FoxitSymbol.cff")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVALIDATE_PREFIX=${_stage}" -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err
)
if(_result EQUAL 0 OR NOT "${_out}${_err}" MATCHES "Missing mandatory PDF font resource")
    message(FATAL_ERROR "Missing runtime font was not rejected: ${_out}${_err}")
endif()

mengshee_stage_pdf_base14_fonts("${SOURCE_ROOT}" "${_install}" "${_stage}")
file(REMOVE "${_stage}/share/fonts/LICENSE.pdfium")
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-DVALIDATE_PREFIX=${_stage}" -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _out ERROR_VARIABLE _err
)
if(_result EQUAL 0 OR NOT "${_out}${_err}" MATCHES "Missing mandatory PDF font resource")
    message(FATAL_ERROR "Missing runtime license was not rejected: ${_out}${_err}")
endif()

file(REMOVE_RECURSE "${_scratch}")
message(STATUS "PDF Base14 sources, deploy, stage, hashes, missing fonts and license checks passed.")
