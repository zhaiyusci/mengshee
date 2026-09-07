# SPDX-License-Identifier: BSD-3-Clause

find_path(Tesseract_INCLUDE_DIR
    NAMES tesseract/baseapi.h
)

find_library(Tesseract_LIBRARY
    NAMES tesseract tesseract55 libtesseract
)

set(Tesseract_VERSION "")
if(Tesseract_INCLUDE_DIR AND EXISTS "${Tesseract_INCLUDE_DIR}/tesseract/version.h")
    file(STRINGS "${Tesseract_INCLUDE_DIR}/tesseract/version.h" _tesseract_version_line
        REGEX "^#define[ \t]+TESSERACT_VERSION_STR[ \t]+\"")
    if(_tesseract_version_line MATCHES "TESSERACT_VERSION_STR[ \t]+\"([^\"]+)\"")
        set(Tesseract_VERSION "${CMAKE_MATCH_1}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Tesseract
    REQUIRED_VARS Tesseract_LIBRARY Tesseract_INCLUDE_DIR
    VERSION_VAR Tesseract_VERSION
)

if(Tesseract_FOUND AND NOT TARGET Tesseract::Tesseract)
    add_library(Tesseract::Tesseract UNKNOWN IMPORTED)
    set_target_properties(Tesseract::Tesseract PROPERTIES
        IMPORTED_LOCATION "${Tesseract_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Tesseract_INCLUDE_DIR}"
    )
    if(WIN32)
        set_property(TARGET Tesseract::Tesseract APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS TESS_IMPORTS)
    endif()
endif()

mark_as_advanced(Tesseract_INCLUDE_DIR Tesseract_LIBRARY)
