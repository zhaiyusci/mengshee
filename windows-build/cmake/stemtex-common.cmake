cmake_minimum_required(VERSION 3.20)

# Resolves the StemTeX runtime that Mengshee renders LaTeX notes with.
#
# StemTeX is prebuilt: the build uses the runtime where it is already installed
# (the StemTeX setup installs into e.g. C:\StemTeX and registers that location),
# and the installed version also decides which QScintilla files the LaTeX note
# editor compiles against. Nothing of StemTeX is compiled here and the runtime is
# used in place, never copied into the workspace.
#
# Resolution order:
#   1. -DSTEMTEX_STAGE_ROOT=<dir>
#   2. a StemTeX installation (registry InstallLocation, then common paths)
#   3. a runtime prepared inside the workspace (<workspace>/stemtex/StemTeX-*)
# When -DSTEMTEX_VERSION=<version> is given, only a runtime of that version is
# accepted; if none is available the prepare step installs it into the workspace.

# Release used only when no StemTeX runtime is available and no version was
# requested.
set(MENGSHEE_STEMTEX_DEFAULT_RELEASE "0.15.2")

function(mengshee_stemtex_usable out_var directory)
    if(EXISTS "${directory}/runtime/VERSION"
       AND EXISTS "${directory}/runtime/bin/sdk/stemtex-renderer.dll"
       AND EXISTS "${directory}/runtime/bin/sdk/stemtex-profile.dll"
       AND EXISTS "${directory}/runtime/bin/windows/stemtex-worker-host.exe"
       AND EXISTS "${directory}/gui/stemtex-profile-creator.exe")
        file(READ "${directory}/runtime/VERSION" _runtime_version)
        string(STRIP "${_runtime_version}" _runtime_version)
        if(_runtime_version VERSION_LESS "0.15.2")
            set(${out_var} FALSE PARENT_SCOPE)
            return()
        endif()
        set(${out_var} TRUE PARENT_SCOPE)
    else()
        set(${out_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

function(mengshee_stemtex_version_of out_var directory)
    file(READ "${directory}/runtime/VERSION" _version)
    string(STRIP "${_version}" _version)
    set(${out_var} "${_version}" PARENT_SCOPE)
endfunction()

# The Inno setup registers where it installed the runtime.
function(mengshee_stemtex_registry_location out_var)
    set(_appid "{7B625C5A-3D8E-48D8-B65A-5357444E8C62}_is1")
    foreach(_hive IN ITEMS
            "HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
            "HKLM\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
            "HKCU\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall")
        execute_process(COMMAND reg query "${_hive}\\${_appid}" /v InstallLocation
                        OUTPUT_VARIABLE _output ERROR_QUIET RESULT_VARIABLE _result)
        if(_result EQUAL 0 AND _output MATCHES "InstallLocation[ \t]+REG_[A-Z_]+[ \t]+([^\r\n]+)")
            set(_location "${CMAKE_MATCH_1}")
            string(STRIP "${_location}" _location)
            string(REPLACE "\\" "/" _location "${_location}")
            if(_location)
                set(${out_var} "${_location}" PARENT_SCOPE)
                return()
            endif()
        endif()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Finds the runtime to build against. Returns an empty result when none is
# available, which tells the prepare step to install the requested release.
function(mengshee_find_stemtex_runtime out_var workspace_root)
    set(_requested "")
    if(DEFINED STEMTEX_VERSION AND NOT "${STEMTEX_VERSION}" STREQUAL "")
        set(_requested "${STEMTEX_VERSION}")
    endif()

    if(DEFINED STEMTEX_STAGE_ROOT AND NOT "${STEMTEX_STAGE_ROOT}" STREQUAL "")
        get_filename_component(_candidate "${STEMTEX_STAGE_ROOT}" ABSOLUTE)
        mengshee_stemtex_usable(_usable "${_candidate}")
        if(NOT _usable)
            message(FATAL_ERROR "STEMTEX_STAGE_ROOT requires a complete StemTeX 0.15.2 or newer runtime: ${_candidate}")
        endif()
        if(_requested)
            mengshee_stemtex_version_of(_version "${_candidate}")
            if(NOT _version STREQUAL _requested)
                message(FATAL_ERROR
                    "STEMTEX_STAGE_ROOT is StemTeX ${_version} but -DSTEMTEX_VERSION=${_requested} was requested.")
            endif()
        endif()
        set(${out_var} "${_candidate}" PARENT_SCOPE)
        return()
    endif()

    set(_candidates)
    # A runtime prepared inside the workspace wins when its version was requested.
    if(_requested)
        list(APPEND _candidates "${workspace_root}/stemtex/StemTeX-${_requested}")
    endif()
    mengshee_stemtex_registry_location(_registry)
    if(_registry)
        list(APPEND _candidates "${_registry}")
    endif()
    list(APPEND _candidates
        "C:/StemTeX"
        "C:/Program Files/StemTeX"
        "$ENV{LOCALAPPDATA}/Programs/StemTeX"
        "$ENV{ProgramFiles}/StemTeX")
    file(GLOB _workspace_runtimes LIST_DIRECTORIES true "${workspace_root}/stemtex/StemTeX-*")
    list(APPEND _candidates ${_workspace_runtimes})

    foreach(_candidate IN LISTS _candidates)
        if(NOT IS_DIRECTORY "${_candidate}")
            continue()
        endif()
        mengshee_stemtex_usable(_usable "${_candidate}")
        if(NOT _usable)
            continue()
        endif()
        if(_requested)
            mengshee_stemtex_version_of(_version "${_candidate}")
            if(NOT _version STREQUAL _requested)
                continue()
            endif()
        endif()
        get_filename_component(_candidate "${_candidate}" ABSOLUTE)
        set(${out_var} "${_candidate}" PARENT_SCOPE)
        return()
    endforeach()
    set(${out_var} "" PARENT_SCOPE)
endfunction()

# Convenience wrapper: resolves the runtime and reports its version.
function(mengshee_resolve_stemtex_runtime stage_out version_out workspace_root)
    mengshee_find_stemtex_runtime(_stage "${workspace_root}")
    if(NOT _stage)
        set(${stage_out} "" PARENT_SCOPE)
        set(${version_out} "" PARENT_SCOPE)
        return()
    endif()
    mengshee_stemtex_version_of(_version "${_stage}")
    set(${stage_out} "${_stage}" PARENT_SCOPE)
    set(${version_out} "${_version}" PARENT_SCOPE)
endfunction()

# Everything the runtime deployment and the packaging step read out of the
# runtime.
function(mengshee_validate_stemtex_runtime stage)
    foreach(_required IN ITEMS
            "runtime/VERSION"
            "runtime/bin/sdk/stemtex-renderer.dll"
            "runtime/bin/sdk/stemtex-profile.dll"
            "runtime/bin/windows/stemtex-worker-host.exe"
            "runtime/bin/windows/xetexdaemon.exe"
            "runtime/bin/windows/xdvipdfmxdaemon.exe"
            "runtime/bin/windows/dvipdfmxdaemon.dll"
            "runtime/bin/windows/dvisvgmdaemon.exe"
            "runtime/bin/windows/dvisvgmdaemon.dll"
            "gui/stemtex-profile-creator.exe"
            "gui/profiles/unicodemath/preamble.tex"
            "gui/profiles/unicodemath/warmup.tex"
            "runtime/texmf-dist/web2c/texmf.cnf")
        if(NOT EXISTS "${stage}/${_required}")
            message(FATAL_ERROR "The StemTeX runtime ${stage} is incomplete, missing ${_required}.")
        endif()
    endforeach()
endfunction()
