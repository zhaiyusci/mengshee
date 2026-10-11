cmake_minimum_required(VERSION 3.20)

# Shared StemTeX pin and lookup helpers.
#
# StemTeX is a prebuilt runtime for this build: the release installer provides
# the runtime, the GUI and the bundled texmf tree, and the matching StemTeX tag
# provides the QScintilla files the LaTeX note editor compiles against. There is
# no StemTeX submodule and nothing of StemTeX is compiled here.

# Single place that decides which StemTeX release this build uses.
function(mengshee_resolve_stemtex_version out_var)
    if(DEFINED STEMTEX_VERSION AND NOT "${STEMTEX_VERSION}" STREQUAL "")
        set(${out_var} "${STEMTEX_VERSION}" PARENT_SCOPE)
    else()
        set(${out_var} "0.14.0" PARENT_SCOPE)
    endif()
endfunction()

# Resolves the directory the runtime was installed into. The caller supplies the
# workspace root so the layout stays <workspace>/stemtex/StemTeX-<version>.
function(mengshee_resolve_stemtex_stage_dir out_var workspace_root version)
    if(DEFINED STEMTEX_STAGE_ROOT AND NOT "${STEMTEX_STAGE_ROOT}" STREQUAL "")
        get_filename_component(_stage "${STEMTEX_STAGE_ROOT}" ABSOLUTE)
    else()
        get_filename_component(_stage "${workspace_root}/stemtex/StemTeX-${version}" ABSOLUTE)
    endif()
    set(${out_var} "${_stage}" PARENT_SCOPE)
endfunction()

# Fails with the alternatives spelled out when the runtime is not usable.
function(mengshee_validate_stemtex_stage stage version)
    if(NOT EXISTS "${stage}/runtime/VERSION")
        message(FATAL_ERROR
            "Cannot find the StemTeX runtime stage ${stage}.\n"
            "Run windows-build/cmake/prepare-stemtex-runtime.cmake (needs administrator rights once),\n"
            "or install StemTeX ${version} yourself and pass -DSTEMTEX_STAGE_ROOT=<directory>.")
    endif()
    if(NOT "${version}" STREQUAL "")
        file(READ "${stage}/runtime/VERSION" _stage_version)
        string(STRIP "${_stage_version}" _stage_version)
        if(NOT _stage_version STREQUAL version)
            message(FATAL_ERROR
                "The StemTeX stage ${stage} is version ${_stage_version}, but this build expects ${version}.\n"
                "Pass -DSTEMTEX_VERSION=${_stage_version} to use it, or install StemTeX ${version}.")
        endif()
    endif()
endfunction()
