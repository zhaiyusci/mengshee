# Shared helper for the Windows build scripts: make a built tree startable with
# the rights of a normal user, or fail with the command that does it.
#
# Build output created inside a restricted (low integrity) session inherits a low
# mandatory integrity label, and Windows starts a process at the lower of the
# user's integrity level and the executable's label. An application launched from
# such a tree runs with reduced rights: it cannot write %TEMP% or %LOCALAPPDATA%,
# so saving fails ("could not open the temporary file for saving"), the KIO worker
# cannot create its socket, and per-user files are redirected to AppData\LocalLow.
# The tree is otherwise correct, which is exactly why the build has to check it
# instead of leaving the developer with a build that misbehaves at runtime.
#
# mengshee_prepare_run_tree(<tree> [<probe>]) rewrites the label of <tree> to
# Medium and verifies it by reading the labels back. <probe> is the tree-relative
# file that decides runnability: `bin/mengshee.exe` for a runtime, and the
# produced `Mengshee-<version>-Setup.exe` for an installer directory. Pass
# -DALLOW_LOW_INTEGRITY=ON to record the failure as a warning instead (for builds
# that deliberately run inside a restricted session and will be relabelled later).

get_filename_component(_mengshee_run_tree_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
set(MENGSHEE_RUN_TREE_SCRIPT "${_mengshee_run_tree_dir}/../scripts/make-runnable.ps1" CACHE INTERNAL "Script that restores the run-time integrity label")
if(NOT DEFINED MENGSHEE_POWERSHELL OR "${MENGSHEE_POWERSHELL}" STREQUAL "")
    set(MENGSHEE_POWERSHELL "powershell.exe")
endif()

function(mengshee_prepare_run_tree tree)
    if(NOT IS_DIRECTORY "${tree}")
        return()
    endif()
    if(NOT EXISTS "${MENGSHEE_RUN_TREE_SCRIPT}")
        message(FATAL_ERROR "Missing run tree script: ${MENGSHEE_RUN_TREE_SCRIPT}")
    endif()
    set(_mengshee_probe "bin/mengshee.exe")
    if(ARGC GREATER 1)
        set(_mengshee_probe "${ARGV1}")
    endif()

    execute_process(
        COMMAND "${MENGSHEE_POWERSHELL}" -ExecutionPolicy Bypass -NoProfile -File "${MENGSHEE_RUN_TREE_SCRIPT}" -Root "${tree}" -Probe "${_mengshee_probe}"
        RESULT_VARIABLE _mengshee_run_tree_result
        OUTPUT_VARIABLE _mengshee_run_tree_output
        ERROR_VARIABLE _mengshee_run_tree_error
    )
    string(STRIP "${_mengshee_run_tree_output}${_mengshee_run_tree_error}" _mengshee_run_tree_report)
    if(NOT "${_mengshee_run_tree_report}" STREQUAL "")
        message(STATUS "${_mengshee_run_tree_report}")
    endif()

    if(NOT _mengshee_run_tree_result EQUAL 0)
        set(_mengshee_run_tree_fix "icacls \"${tree}/${_mengshee_probe}\" /setintegritylevel Medium")
        # Relay what the check actually found instead of guessing: a missing file
        # and a reduced-integrity label are both reported as "not runnable" here,
        # and only the second one is fixed by the icacls command below.
        if("${_mengshee_run_tree_report}" STREQUAL "")
            set(_mengshee_run_tree_report "the check reported no reason")
        endif()
        if(DEFINED ALLOW_LOW_INTEGRITY AND ALLOW_LOW_INTEGRITY)
            message(WARNING
                "${tree}/${_mengshee_probe} is not runnable with normal rights: ${_mengshee_run_tree_report}\n"
                "When the cause is its mandatory integrity label, run this once in a normal (unelevated) PowerShell:\n  ${_mengshee_run_tree_fix}")
        else()
            message(FATAL_ERROR
                "${tree}/${_mengshee_probe} is not runnable with normal rights: ${_mengshee_run_tree_report}\n"
                "A runtime that starts with reduced rights cannot save (no writable %TEMP%), and an installer that starts that way cannot unpack its own files.\n"
                "When the cause is its mandatory integrity label, run this once in a normal (unelevated) PowerShell and build again:\n  ${_mengshee_run_tree_fix}\n"
                "Pass -DALLOW_LOW_INTEGRITY=ON to build anyway and repair that later.")
        endif()
    else()
        set_property(GLOBAL APPEND PROPERTY MENGSHEE_RUN_TREES "${tree}/${_mengshee_probe}")
    endif()
endfunction()

# Prints the files a developer can start once the build has finished.
function(mengshee_report_run_trees)
    get_property(_mengshee_run_trees GLOBAL PROPERTY MENGSHEE_RUN_TREES)
    if(NOT _mengshee_run_trees)
        return()
    endif()
    message(STATUS "Runnable build output (normal user rights):")
    foreach(_mengshee_tree IN LISTS _mengshee_run_trees)
        message(STATUS "  ${_mengshee_tree}")
    endforeach()
endfunction()
