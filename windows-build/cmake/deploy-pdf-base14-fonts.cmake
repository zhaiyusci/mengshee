# Standalone entry for SDK/development deployment; implementation is shared
# with the main runtime and installer packaging paths.
if(NOT DEFINED SOURCE_ROOT OR NOT DEFINED PREFIX)
    message(FATAL_ERROR "SOURCE_ROOT and PREFIX are required.")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/pdf-base14-fonts.cmake")
mengshee_deploy_pdf_base14_fonts("${SOURCE_ROOT}" "${PREFIX}")
