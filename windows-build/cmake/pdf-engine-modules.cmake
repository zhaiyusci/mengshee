# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Helpers for the phase-2 PDF runtime: one Okular plugin plus loadable MuPDF and
# Poppler engine modules.
#
# MENGSHEE_PDF_ENGINE_MODULES selects the deployment shape:
#   OFF (default) - one backend, the legacy generator plugin only.
#   ON            - both engines, the single plugin, and both legacy plugins for
#                   the compatibility fallback.
#
# MUPDF_ENGINE_ROOT / POPPLER_ENGINE_ROOT may point at an install or a build
# tree whose bin/ contains the engine module (and, for MuPDF,
# MengsheePdfRuntime.dll). When unset, the canonical workspace directories are
# searched. A build tree is accepted so the deployment can consume a
# configuration that was built but not installed yet.

function(mengshee_pdf_engine_modules_enabled out_var)
    if(NOT DEFINED MENGSHEE_PDF_ENGINE_MODULES OR "${MENGSHEE_PDF_ENGINE_MODULES}" STREQUAL "")
        set(_enabled OFF)
    elseif(MENGSHEE_PDF_ENGINE_MODULES)
        set(_enabled ON)
    else()
        set(_enabled OFF)
    endif()
    set(${out_var} "${_enabled}" PARENT_SCOPE)
endfunction()

function(mengshee_pdf_engine_module_name engine out_var)
    if(engine STREQUAL "MuPDF")
        set(${out_var} "MengsheePdfEngineMuPdf.dll" PARENT_SCOPE)
    elseif(engine STREQUAL "Poppler")
        set(${out_var} "MengsheePdfEnginePoppler.dll" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unknown PDF engine '${engine}'; expected MuPDF or Poppler")
    endif()
endfunction()

function(mengshee_pdf_engine_legacy_plugin_name engine out_var)
    if(engine STREQUAL "MuPDF")
        set(${out_var} "okularGenerator_mupdf.dll" PARENT_SCOPE)
    elseif(engine STREQUAL "Poppler")
        set(${out_var} "okularGenerator_poppler.dll" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unknown PDF engine '${engine}'; expected MuPDF or Poppler")
    endif()
endfunction()

# mengshee_pdf_find_first_file(<out_var> <candidate>...)
function(mengshee_pdf_find_first_file out_var)
    foreach(_candidate IN LISTS ARGN)
        if(NOT "${_candidate}" STREQUAL "" AND EXISTS "${_candidate}")
            get_filename_component(_absolute "${_candidate}" ABSOLUTE)
            set(${out_var} "${_absolute}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Resolves the engine module in an install tree
# (bin/plugins/mengshee_pdf) or a build tree (bin).
function(mengshee_pdf_engine_module_path out_var root engine)
    mengshee_pdf_engine_module_name("${engine}" _module_name)
    mengshee_pdf_find_first_file(_module
        "${root}/bin/plugins/mengshee_pdf/${_module_name}"
        "${root}/bin/${_module_name}"
    )
    set(${out_var} "${_module}" PARENT_SCOPE)
endfunction()

# Resolves an Okular generator plugin in an install tree
# (bin/plugins/okular_generators) or a build tree (bin/okular_generators).
function(mengshee_pdf_generator_path out_var root file_name)
    mengshee_pdf_find_first_file(_generator
        "${root}/bin/plugins/okular_generators/${file_name}"
        "${root}/bin/okular_generators/${file_name}"
    )
    set(${out_var} "${_generator}" PARENT_SCOPE)
endfunction()

function(mengshee_pdf_single_plugin_path out_var root)
    mengshee_pdf_generator_path(_plugin "${root}" "okularGenerator_mengshee_pdf.dll")
    set(${out_var} "${_plugin}" PARENT_SCOPE)
endfunction()

function(mengshee_pdf_legacy_plugin_path out_var root engine)
    mengshee_pdf_engine_legacy_plugin_name("${engine}" _plugin_name)
    mengshee_pdf_generator_path(_plugin "${root}" "${_plugin_name}")
    set(${out_var} "${_plugin}" PARENT_SCOPE)
endfunction()

function(mengshee_pdf_engine_runtime_path out_var root)
    mengshee_pdf_find_first_file(_runtime
        "${root}/bin/MengsheePdfRuntime.dll"
        "${root}/bin/plugins/mengshee_pdf/MengsheePdfRuntime.dll"
    )
    set(${out_var} "${_runtime}" PARENT_SCOPE)
endfunction()

# Finds a directory containing the requested engine module. The primary
# configuration (the one MENGSHEE_PDF_BACKEND selects) is checked first, then
# the canonical parallel build/install trees in this workspace.
function(mengshee_pdf_engine_root out_var engine)
    mengshee_pdf_engine_module_name("${engine}" _module_name)

    set(_candidates)
    if(engine STREQUAL "MuPDF")
        if(DEFINED MUPDF_ENGINE_ROOT AND NOT "${MUPDF_ENGINE_ROOT}" STREQUAL "")
            list(APPEND _candidates "${MUPDF_ENGINE_ROOT}")
        endif()
        if(MENGSHEE_PDF_BACKEND STREQUAL "MuPDF")
            if(DEFINED INSTALL_PREFIX AND NOT "${INSTALL_PREFIX}" STREQUAL "")
                list(APPEND _candidates "${INSTALL_PREFIX}")
            endif()
            if(DEFINED BUILD_DIR AND NOT "${BUILD_DIR}" STREQUAL "")
                list(APPEND _candidates "${BUILD_DIR}")
            endif()
        endif()
        list(APPEND _candidates
            "${WORKSPACE_ROOT}/build/mengshee-mupdf"
            "${WORKSPACE_ROOT}/build/mengshee-standalone"
            "${WORKSPACE_ROOT}/install/mengshee-mupdf"
            "${WORKSPACE_ROOT}/install/mengshee"
        )
    elseif(engine STREQUAL "Poppler")
        if(DEFINED POPPLER_ENGINE_ROOT AND NOT "${POPPLER_ENGINE_ROOT}" STREQUAL "")
            list(APPEND _candidates "${POPPLER_ENGINE_ROOT}")
        endif()
        if(MENGSHEE_PDF_BACKEND STREQUAL "Poppler")
            if(DEFINED INSTALL_PREFIX AND NOT "${INSTALL_PREFIX}" STREQUAL "")
                list(APPEND _candidates "${INSTALL_PREFIX}")
            endif()
            if(DEFINED BUILD_DIR AND NOT "${BUILD_DIR}" STREQUAL "")
                list(APPEND _candidates "${BUILD_DIR}")
            endif()
        endif()
        list(APPEND _candidates
            "${WORKSPACE_ROOT}/build/mengshee-poppler"
            "${WORKSPACE_ROOT}/install/mengshee-poppler"
        )
    else()
        message(FATAL_ERROR "Unknown PDF engine '${engine}'; expected MuPDF or Poppler")
    endif()

    foreach(_root IN LISTS _candidates)
        if("${_root}" STREQUAL "" OR NOT IS_DIRECTORY "${_root}")
            continue()
        endif()
        mengshee_pdf_engine_module_path(_module "${_root}" "${engine}")
        if(_module)
            get_filename_component(_absolute "${_root}" ABSOLUTE)
            set(${out_var} "${_absolute}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Fails with actionable instructions when a required engine tree is missing.
function(mengshee_pdf_require_engine_root out_var engine)
    mengshee_pdf_engine_root(_root "${engine}")
    if(NOT _root)
        if(engine STREQUAL "MuPDF")
            set(_variable "MUPDF_ENGINE_ROOT")
            set(_directory "mengshee-mupdf")
        else()
            set(_variable "POPPLER_ENGINE_ROOT")
            set(_directory "mengshee-poppler")
        endif()
        message(FATAL_ERROR
            "MENGSHEE_PDF_ENGINE_MODULES is ON, but no ${engine} engine module was found.\n"
            "Searched the ${engine} configuration and the canonical workspace trees.\n"
            "Build that configuration first, for example:\n"
            "  cmake -DMENGSHEE_PDF_BACKEND=${engine} \\\n"
            "        -DBUILD_DIR=${WORKSPACE_ROOT}/build/${_directory} \\\n"
            "        -DINSTALL_PREFIX=${WORKSPACE_ROOT}/install/${_directory} \\\n"
            "        -DSKIP_DEPLOY=ON -DSKIP_PACKAGE=ON \\\n"
            "        -P windows-build/cmake/build-windows.cmake\n"
            "or pass -D${_variable}=<install-or-build-directory>."
        )
    endif()
    set(${out_var} "${_root}" PARENT_SCOPE)
endfunction()
