cmake_minimum_required(VERSION 3.20)

get_filename_component(_script_dir "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)
get_filename_component(_windows_build_dir "${_script_dir}/.." ABSOLUTE)
get_filename_component(_default_source_root "${_windows_build_dir}/.." ABSOLUTE)
get_filename_component(_default_workspace_root "${_default_source_root}/windows-build-workspace" ABSOLUTE)
include("${_script_dir}/stemtex-common.cmake")

function(mengshee_default_path variable default_value)
    if(NOT DEFINED ${variable} OR "${${variable}}" STREQUAL "")
        set(${variable} "${default_value}" PARENT_SCOPE)
    else()
        get_filename_component(_absolute_value "${${variable}}" ABSOLUTE)
        set(${variable} "${_absolute_value}" PARENT_SCOPE)
    endif()
endfunction()

function(mengshee_find_qt_prefix out_var)
    set(_candidates)
    if(DEFINED QT_PREFIX AND NOT "${QT_PREFIX}" STREQUAL "")
        list(APPEND _candidates "${QT_PREFIX}")
    endif()
    if(DEFINED ENV{QTDIR})
        list(APPEND _candidates "$ENV{QTDIR}")
    endif()
    file(GLOB _qt_roots LIST_DIRECTORIES true "C:/Qt/*/msvc2022_64")
    list(APPEND _candidates ${_qt_roots})
    foreach(_candidate IN LISTS _candidates)
        if(EXISTS "${_candidate}/lib/cmake/Qt6/Qt6Config.cmake")
            get_filename_component(_qt "${_candidate}" ABSOLUTE)
            set(${out_var} "${_qt}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Cannot find a Qt MSVC prefix. Pass -DQT_PREFIX=C:/Qt/<version>/msvc2022_64.")
endfunction()

function(mengshee_find_qscintilla_root out_var)
    set(_candidates)
    if(DEFINED QSCINTILLA_ROOT AND NOT "${QSCINTILLA_ROOT}" STREQUAL "")
        list(APPEND _candidates "${QSCINTILLA_ROOT}")
    endif()
    if(DEFINED ENV{MENGSHEE_QSCINTILLA_ROOT})
        list(APPEND _candidates "$ENV{MENGSHEE_QSCINTILLA_ROOT}")
    endif()
    # Prepared by prepare-stemtex-runtime.cmake from the pinned StemTeX tag.
    if(DEFINED WORKSPACE_ROOT AND NOT "${WORKSPACE_ROOT}" STREQUAL "")
        list(APPEND _candidates "${WORKSPACE_ROOT}/stemtex/qscintilla/third_party")
    endif()
    if(DEFINED STEMTEX_ROOT AND NOT "${STEMTEX_ROOT}" STREQUAL "")
        list(APPEND _candidates "${STEMTEX_ROOT}/third_party")
    endif()
    if(DEFINED ENV{MENGSHEE_STEMTEX_SOURCE_ROOT})
        list(APPEND _candidates "$ENV{MENGSHEE_STEMTEX_SOURCE_ROOT}/third_party")
    endif()

    foreach(_candidate IN LISTS _candidates)
        if(EXISTS "${_candidate}/qscintilla-src/src/Qsci/qsciscintilla.h"
            AND EXISTS "${_candidate}/qscintilla-build/release/qscintilla2_qt6.dll")
            get_filename_component(_qscintilla "${_candidate}" ABSOLUTE)
            set(${out_var} "${_qscintilla}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Cannot find the QScintilla runtime. Pass -DQSCINTILLA_ROOT=<path>.")
endfunction()

# Resolves the StemTeX runtime. StemTeX is prebuilt and used where it is
# installed (see stemtex-common.cmake); only the stage and its version are needed
# here. build-windows.cmake passes both explicitly.
function(mengshee_resolve_stemtex_stage out_var)
    mengshee_resolve_stemtex_runtime(_stage _version "${WORKSPACE_ROOT}")
    if(NOT _stage)
        message(FATAL_ERROR
            "No StemTeX runtime found. Run windows-build/cmake/prepare-stemtex-runtime.cmake first\n"
            "(it uses an installed StemTeX in place) or pass -DSTEMTEX_STAGE_ROOT=<directory>.")
    endif()
    mengshee_validate_stemtex_runtime("${_stage}")

    # A runtime that has been used already contains warm-up output under
    # gui/profiles. It is not packaged: the renderer regenerates it, keyed to the
    # runtime and the selected TeX tree. The copy below filters it out.
    file(GLOB_RECURSE _generated_profile_files
        "${_stage}/gui/profiles/*.aux"
        "${_stage}/gui/profiles/*.log"
        "${_stage}/gui/profiles/*.pdf"
        "${_stage}/gui/profiles/*.xdv"
        "${_stage}/gui/profiles/*.synctex.gz"
    )
    if(_generated_profile_files)
        list(LENGTH _generated_profile_files _generated_count)
        message(STATUS "The StemTeX runtime carries ${_generated_count} warm-up file(s); they are excluded from the package")
    endif()

    set(${out_var} "${_stage}" PARENT_SCOPE)
endfunction()

function(mengshee_remove_inside path allowed_root)
    get_filename_component(_path "${path}" ABSOLUTE)
    get_filename_component(_allowed "${allowed_root}" ABSOLUTE)
    string(FIND "${_path}" "${_allowed}" _position)
    if(NOT _position EQUAL 0)
        message(FATAL_ERROR "Refusing to remove path outside ${_allowed}: ${_path}")
    endif()
    if(EXISTS "${_path}")
        file(REMOVE_RECURSE "${_path}")
    endif()
endfunction()

function(mengshee_remove_matching_files directory pattern)
    if(NOT IS_DIRECTORY "${directory}")
        return()
    endif()
    file(GLOB _files "${directory}/${pattern}")
    foreach(_file IN LISTS _files)
        if(NOT IS_DIRECTORY "${_file}")
            file(REMOVE "${_file}")
        endif()
    endforeach()
endfunction()

if(NOT DEFINED MENGSHEE_RUNTIME_LOCALES OR "${MENGSHEE_RUNTIME_LOCALES}" STREQUAL "")
    set(MENGSHEE_RUNTIME_LOCALES zh_CN)
endif()

if("zh_CN" IN_LIST MENGSHEE_RUNTIME_LOCALES)
    foreach(_catalog IN ITEMS
        kcolorscheme6.mo
        kconfigwidgets6.mo
        ki18n6.mo
        kiconthemes6.mo
        kio6.mo
        kparts6.mo
        kservice6.mo
        ktextwidgets6.mo
        kxmlgui6.mo
    )
        set(_catalog_path "${SDK_PREFIX}/bin/data/locale/zh_CN/LC_MESSAGES/${_catalog}")
        if(NOT EXISTS "${_catalog_path}")
            message(FATAL_ERROR "Missing required KF6 Simplified Chinese catalog: ${_catalog_path}")
        endif()
        file(SIZE "${_catalog_path}" _catalog_size)
        if(_catalog_size LESS_EQUAL 28)
            message(FATAL_ERROR
                "Required KF6 Simplified Chinese catalog is empty: ${_catalog_path}. "
                "Rebuild the corresponding KF6 SDK module with the native GNU msgfmt tool.")
        endif()
    endforeach()
endif()

function(mengshee_prune_locale_tree locale_root allowed_root)
    if(NOT IS_DIRECTORY "${locale_root}")
        return()
    endif()

    file(GLOB _locale_dirs LIST_DIRECTORIES true "${locale_root}/*")
    foreach(_locale_dir IN LISTS _locale_dirs)
        if(NOT IS_DIRECTORY "${_locale_dir}")
            continue()
        endif()
        get_filename_component(_locale_name "${_locale_dir}" NAME)
        if(NOT _locale_name IN_LIST MENGSHEE_RUNTIME_LOCALES)
            mengshee_remove_inside("${_locale_dir}" "${allowed_root}")
        endif()
    endforeach()
endfunction()

function(mengshee_sync_tree source destination allowed_root)
    get_filename_component(_source "${source}" ABSOLUTE)
    get_filename_component(_destination "${destination}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${_source}")
        return()
    endif()
    mengshee_remove_inside("${_destination}" "${allowed_root}")
    file(MAKE_DIRECTORY "${_destination}")
    file(COPY "${_source}/" DESTINATION "${_destination}")
endfunction()

function(mengshee_copy_tree_contents source destination)
    get_filename_component(_source "${source}" ABSOLUTE)
    get_filename_component(_destination "${destination}" ABSOLUTE)
    if(NOT IS_DIRECTORY "${_source}")
        return()
    endif()
    file(MAKE_DIRECTORY "${_destination}")
    file(COPY "${_source}/" DESTINATION "${_destination}")
endfunction()

function(mengshee_copy_file source destination)
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "Missing file: ${source}")
    endif()
    get_filename_component(_destination_dir "${destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${_destination_dir}")
    file(COPY_FILE "${source}" "${destination}" ONLY_IF_DIFFERENT)
endfunction()

function(mengshee_remove_empty_gettext_catalogs root)
    if(NOT IS_DIRECTORY "${root}")
        return()
    endif()
    file(GLOB_RECURSE _catalogs "${root}/*.mo")
    foreach(_catalog IN LISTS _catalogs)
        file(SIZE "${_catalog}" _size)
        if(_size LESS_EQUAL 28)
            file(REMOVE "${_catalog}")
        endif()
    endforeach()
endfunction()

function(mengshee_copy_mengshee_runtime_data prefix)
    foreach(_size IN ITEMS 16 22 32 48 64 128 256)
        mengshee_copy_file(
            "${SOURCE_ROOT}/icons/${_size}-apps-mengshee.png"
            "${prefix}/bin/data/icons/hicolor/${_size}x${_size}/apps/mengshee.png"
        )
        mengshee_copy_file(
            "${SOURCE_ROOT}/icons/${_size}-mimetypes-application-pdf.png"
            "${prefix}/bin/data/icons/hicolor/${_size}x${_size}/mimetypes/application-pdf.png"
        )
    endforeach()

    mengshee_copy_file("${SOURCE_ROOT}/icons/mengshee.ico" "${prefix}/bin/mengshee.ico")
    mengshee_copy_file("${SOURCE_ROOT}/icons/application-pdf.ico" "${prefix}/bin/application-pdf.ico")
    mengshee_copy_file("${SOURCE_ROOT}/shell/org.jairy.mengshee.desktop" "${prefix}/bin/data/applications/org.jairy.mengshee.desktop")
    mengshee_copy_file("${SOURCE_ROOT}/shell/org.jairy.mengshee.appdata.xml" "${prefix}/bin/data/metainfo/org.jairy.mengshee.appdata.xml")
    mengshee_copy_file("${SOURCE_ROOT}/mengshee.categories" "${prefix}/bin/data/qlogging-categories6/mengshee.categories")

    foreach(_file IN ITEMS tools.xml toolsQuick.xml drawingtools.xml)
        mengshee_copy_file("${SOURCE_ROOT}/part/data/${_file}" "${prefix}/bin/data/mengshee/${_file}")
    endforeach()

    mengshee_copy_file("${SDK_PREFIX}/share/tessdata/eng.traineddata" "${prefix}/bin/data/mengshee/tessdata/eng.traineddata")

    mengshee_copy_file("${SOURCE_ROOT}/core/stamps.svg" "${prefix}/bin/data/mengshee/pics/stamps.svg")
    file(GLOB _pic_files "${SOURCE_ROOT}/part/data/*.png" "${SOURCE_ROOT}/part/data/*.svg")
    foreach(_pic IN LISTS _pic_files)
        get_filename_component(_name "${_pic}" NAME)
        mengshee_copy_file("${_pic}" "${prefix}/bin/data/mengshee/pics/${_name}")
    endforeach()
endfunction()

function(mengshee_copy_translations workspace_root prefix)
    set(_source_root "${workspace_root}/build/mengshee-standalone/locale")
    if(BUILD_DIR)
        set(_source_root "${BUILD_DIR}/locale")
    endif()
    if(NOT IS_DIRECTORY "${_source_root}")
        message(FATAL_ERROR "Missing Mengshee translation build directory: ${_source_root}")
    endif()

    file(GLOB_RECURSE _catalogs "${_source_root}/*.mo")
    set(_copied 0)
    foreach(_catalog IN LISTS _catalogs)
        file(SIZE "${_catalog}" _size)
        if(_size GREATER 28)
            file(RELATIVE_PATH _relative "${_source_root}" "${_catalog}")
            string(REPLACE "/" ";" _relative_parts "${_relative}")
            list(GET _relative_parts 0 _locale_name)
            if(_locale_name IN_LIST MENGSHEE_RUNTIME_LOCALES)
                mengshee_copy_file("${_catalog}" "${prefix}/bin/data/locale/${_relative}")
                math(EXPR _copied "${_copied} + 1")
            endif()
        endif()
    endforeach()
    if(_copied EQUAL 0)
        message(FATAL_ERROR "No non-empty Mengshee translation catalogs were found under ${_source_root}")
    endif()
    message(STATUS "Copied ${_copied} Mengshee translation catalogs.")
endfunction()

function(mengshee_copy_qt_chinese_translations qt_root destination_bin_dir)
    foreach(_file IN ITEMS qt_zh_CN.qm qtbase_zh_CN.qm)
        if(EXISTS "${qt_root}/translations/${_file}")
            mengshee_copy_file("${qt_root}/translations/${_file}" "${destination_bin_dir}/translations/${_file}")
        else()
            message(WARNING "Skipping missing Qt translation: ${_file}")
        endif()
    endforeach()
endfunction()

function(mengshee_write_qt_conf destination_bin_dir)
    file(WRITE "${destination_bin_dir}/qt.conf" "[Paths]\nPrefix=.\nPlugins=plugins\nTranslations=translations\n")
endfunction()

function(mengshee_copy_plugin source_root relative_path destination_root)
    if(EXISTS "${source_root}/${relative_path}")
        mengshee_copy_file("${source_root}/${relative_path}" "${destination_root}/${relative_path}")
    else()
        message(WARNING "Skipping missing plugin: ${relative_path}")
    endif()
endfunction()

mengshee_default_path(SOURCE_ROOT "${_default_source_root}")
mengshee_default_path(WORKSPACE_ROOT "${_default_workspace_root}")
mengshee_default_path(SDK_PREFIX "${WORKSPACE_ROOT}/sdk")
mengshee_default_path(INSTALL_PREFIX "${WORKSPACE_ROOT}/install/mengshee")
mengshee_find_qt_prefix(QT_PREFIX_RESOLVED)
set(QT_PREFIX "${QT_PREFIX_RESOLVED}")
mengshee_find_qscintilla_root(QSCINTILLA_ROOT_RESOLVED)
mengshee_resolve_stemtex_stage(STEMTEX_STAGE_ROOT_RESOLVED)
set(STEMTEX_RUNTIME_SOURCE "${STEMTEX_STAGE_ROOT_RESOLVED}/runtime")
set(STEMTEX_BASIC_PROFILE_SOURCE "${STEMTEX_STAGE_ROOT_RESOLVED}/gui/profiles/unicodemath")

find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt.exe HINTS "${QT_PREFIX}/bin")
if(DEFINED WINDEPLOYQT AND NOT "${WINDEPLOYQT}" STREQUAL "")
    set(WINDEPLOYQT_EXECUTABLE "${WINDEPLOYQT}")
endif()
if(NOT WINDEPLOYQT_EXECUTABLE)
    message(FATAL_ERROR "Cannot find windeployqt. Pass -DWINDEPLOYQT=<path>.")
endif()

set(_bin_dir "${INSTALL_PREFIX}/bin")
set(_exe "${_bin_dir}/mengshee.exe")
if(NOT EXISTS "${_exe}")
    message(FATAL_ERROR "Cannot find installed Mengshee executable: ${_exe}. Build first.")
endif()

message(STATUS "Mengshee runtime deploy")
message(STATUS "  QtPrefix  : ${QT_PREFIX}")
message(STATUS "  SdkPrefix : ${SDK_PREFIX}")
message(STATUS "  Install   : ${INSTALL_PREFIX}")
message(STATUS "  StemStage : ${STEMTEX_STAGE_ROOT_RESOLVED}")
message(STATUS "  QScintilla: ${QSCINTILLA_ROOT_RESOLVED}")

mengshee_remove_inside("${INSTALL_PREFIX}/bin/data/mengshee/microtex" "${INSTALL_PREFIX}")
file(REMOVE "${INSTALL_PREFIX}/bin/LaTeX.dll" "${INSTALL_PREFIX}/lib/LaTeX.lib")

file(GLOB _sdk_dlls "${SDK_PREFIX}/bin/*.dll")
if(NOT MENGSHEE_PDF_BACKEND)
    set(MENGSHEE_PDF_BACKEND MuPDF)
endif()
foreach(_dll IN LISTS _sdk_dlls)
    get_filename_component(_name "${_dll}" NAME)
    if(MENGSHEE_PDF_BACKEND STREQUAL "MuPDF" AND _name MATCHES "^poppler.*\\.dll$")
        continue()
    endif()
    mengshee_copy_file("${_dll}" "${_bin_dir}/${_name}")
endforeach()
foreach(_required_ocr_runtime IN ITEMS leptonica-1.87.0.dll tesseract55.dll)
    if(NOT EXISTS "${_bin_dir}/${_required_ocr_runtime}")
        message(FATAL_ERROR "Missing English OCR runtime: ${_required_ocr_runtime}")
    endif()
endforeach()
mengshee_copy_file("${QSCINTILLA_ROOT_RESOLVED}/qscintilla-build/release/qscintilla2_qt6.dll" "${_bin_dir}/qscintilla2_qt6.dll")

mengshee_sync_tree("${SDK_PREFIX}/bin/data" "${_bin_dir}/data" "${INSTALL_PREFIX}")
mengshee_remove_empty_gettext_catalogs("${_bin_dir}/data/locale")
mengshee_prune_locale_tree("${_bin_dir}/data/locale" "${INSTALL_PREFIX}")
foreach(_unneeded_data_dir IN ITEMS docs kdevappwizard m4sugar skeletons win_bison_only win_flex_bison win_flex_only xslt)
    mengshee_remove_inside("${_bin_dir}/data/${_unneeded_data_dir}" "${INSTALL_PREFIX}")
endforeach()
mengshee_remove_inside("${_bin_dir}/data/kf6/searchproviders" "${INSTALL_PREFIX}")
foreach(_unneeded_data_file IN ITEMS bison-default.css local.mk README.md)
    file(REMOVE "${_bin_dir}/data/${_unneeded_data_file}")
endforeach()

# Core PDF resources must not depend on system fonts or optional StemTeX.
include("${CMAKE_CURRENT_LIST_DIR}/pdf-base14-fonts.cmake")
mengshee_deploy_pdf_base14_fonts("${SOURCE_ROOT}" "${SDK_PREFIX}")
mengshee_deploy_pdf_base14_fonts("${SOURCE_ROOT}" "${INSTALL_PREFIX}")

if(MENGSHEE_PDF_BACKEND STREQUAL "MuPDF")
    file(REMOVE "${_bin_dir}/poppler.dll" "${_bin_dir}/poppler-qt6.dll")
    mengshee_remove_inside("${INSTALL_PREFIX}/share/poppler" "${INSTALL_PREFIX}")
    mengshee_copy_file("${SOURCE_ROOT}/external/mupdf/COPYING" "${INSTALL_PREFIX}/share/licenses/mupdf/COPYING")
elseif(IS_DIRECTORY "${SDK_PREFIX}/share/poppler")
    mengshee_sync_tree("${SDK_PREFIX}/share/poppler" "${INSTALL_PREFIX}/share/poppler" "${INSTALL_PREFIX}")
else()
    message(WARNING "Poppler CMap/CID data was not found under ${SDK_PREFIX}/share/poppler.")
endif()

set(_staged_plugins "${INSTALL_PREFIX}/.runtime-plugins-staging")
mengshee_remove_inside("${_staged_plugins}" "${INSTALL_PREFIX}")
file(MAKE_DIRECTORY "${_staged_plugins}")
mengshee_copy_plugin("${SDK_PREFIX}/lib/plugins" "kf6/kio/kio_file.dll" "${_staged_plugins}")
mengshee_copy_plugin("${SDK_PREFIX}/lib/plugins" "kiconthemes6/iconengines/KIconEnginePlugin.dll" "${_staged_plugins}")

if(IS_DIRECTORY "${INSTALL_PREFIX}/plugins")
    file(COPY "${INSTALL_PREFIX}/plugins/" DESTINATION "${_staged_plugins}")
else()
    mengshee_copy_plugin("${_bin_dir}/plugins" "kf6/parts/okularpart.dll" "${_staged_plugins}")
    if(MENGSHEE_PDF_BACKEND STREQUAL "MuPDF")
        mengshee_copy_plugin("${_bin_dir}/plugins" "okular_generators/okularGenerator_mupdf.dll" "${_staged_plugins}")
    else()
        mengshee_copy_plugin("${_bin_dir}/plugins" "okular_generators/okularGenerator_poppler.dll" "${_staged_plugins}")
    endif()
endif()
if(MENGSHEE_PDF_BACKEND STREQUAL "MuPDF")
    file(REMOVE "${_staged_plugins}/okular_generators/okularGenerator_poppler.dll")
endif()

mengshee_remove_inside("${_bin_dir}/plugins" "${INSTALL_PREFIX}")
file(RENAME "${_staged_plugins}" "${_bin_dir}/plugins")
mengshee_remove_inside("${INSTALL_PREFIX}/plugins" "${INSTALL_PREFIX}")
mengshee_remove_inside("${INSTALL_PREFIX}/lib/plugins" "${INSTALL_PREFIX}")

mengshee_remove_matching_files("${_bin_dir}" "Qt6*.dll")
file(REMOVE "${_bin_dir}/dxcompiler.dll" "${_bin_dir}/dxil.dll")
execute_process(
    COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime --no-opengl-sw "${_exe}"
    RESULT_VARIABLE _windeployqt_result
)
if(NOT _windeployqt_result EQUAL 0)
    message(FATAL_ERROR "windeployqt failed with exit code ${_windeployqt_result}")
endif()

foreach(_dll IN ITEMS Qt6Concurrent.dll Qt6Qml.dll Qt6QmlMeta.dll Qt6QmlModels.dll)
    mengshee_copy_file("${QT_PREFIX}/bin/${_dll}" "${_bin_dir}/${_dll}")
endforeach()
file(REMOVE "${_bin_dir}/Qt6Quick.dll" "${_bin_dir}/Qt6OpenGL.dll")
file(REMOVE "${_bin_dir}/KF6ConfigQml.dll" "${_bin_dir}/KF6I18nQml.dll")
file(REMOVE "${_bin_dir}/Qt6Pdf.dll")
file(REMOVE "${_bin_dir}/plugins/imageformats/qpdf.dll")

mengshee_copy_qt_chinese_translations("${QT_PREFIX}" "${_bin_dir}")
mengshee_write_qt_conf("${_bin_dir}")

mengshee_remove_inside("${INSTALL_PREFIX}/StemTeX/runtime" "${INSTALL_PREFIX}")
mengshee_remove_inside("${INSTALL_PREFIX}/StemTeX/gui/profiles" "${INSTALL_PREFIX}")
mengshee_copy_tree_contents("${STEMTEX_RUNTIME_SOURCE}" "${INSTALL_PREFIX}/StemTeX/runtime")
mengshee_copy_tree_contents("${STEMTEX_BASIC_PROFILE_SOURCE}" "${INSTALL_PREFIX}/StemTeX/gui/profiles/unicodemath")
# Drop warm-up output that came along with an already used StemTeX runtime: the
# renderer regenerates it in the user cache, keyed to the runtime and TeX tree.
file(GLOB_RECURSE _copied_profile_output
    "${INSTALL_PREFIX}/StemTeX/gui/profiles/*.aux"
    "${INSTALL_PREFIX}/StemTeX/gui/profiles/*.log"
    "${INSTALL_PREFIX}/StemTeX/gui/profiles/*.pdf"
    "${INSTALL_PREFIX}/StemTeX/gui/profiles/*.xdv"
    "${INSTALL_PREFIX}/StemTeX/gui/profiles/*.synctex.gz"
)
if(_copied_profile_output)
    file(REMOVE ${_copied_profile_output})
endif()
mengshee_copy_file("${STEMTEX_STAGE_ROOT_RESOLVED}/gui/stemtex-profile-creator.exe" "${_bin_dir}/stemtex-profile-creator.exe")
mengshee_remove_inside("${INSTALL_PREFIX}/bin/data/icons/breeze" "${INSTALL_PREFIX}")
mengshee_remove_inside("${INSTALL_PREFIX}/bin/data/icons/breeze-dark" "${INSTALL_PREFIX}")
mengshee_copy_mengshee_runtime_data("${INSTALL_PREFIX}")
mengshee_copy_translations("${WORKSPACE_ROOT}" "${INSTALL_PREFIX}")
mengshee_prune_locale_tree("${_bin_dir}/data/locale" "${INSTALL_PREFIX}")
mengshee_remove_inside("${INSTALL_PREFIX}/plugins" "${INSTALL_PREFIX}")
mengshee_remove_inside("${INSTALL_PREFIX}/lib/plugins" "${INSTALL_PREFIX}")

message(STATUS "Runtime deployment complete.")
