remove_definitions(-DTRANSLATION_DOMAIN="okular")
add_definitions(-DTRANSLATION_DOMAIN="okular_poppler")

add_subdirectory( conf )

set(CMAKE_REQUIRED_LIBRARIES Poppler::Qt6 Qt6::Core Qt6::Gui)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <QString>
#include <poppler-qt6.h>

int main()
{
    Poppler::TextAnnotation annotation(Poppler::TextAnnotation::InPlace);
    annotation.setCustomBoolProperty(QStringLiteral(\"OkularTestFlag\"), true);
    annotation.setCustomRealProperty(QStringLiteral(\"OkularTestValue\"), 1.0);
    return 0;
}
" POPPLER_QT6_HAS_ANNOTATION_CUSTOM_SCALAR_PROPERTIES)

include_directories(
   ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

########### next target ###############

set(okularGenerator_poppler_PART_SRCS
   generator_pdf.cpp
   formfields.cpp
   annots.cpp
   pdfsignatureutils.cpp
   pdfsettingswidget.cpp
   imagescaling.cpp
   popplermodule.cpp
)

ki18n_wrap_ui(okularGenerator_poppler_PART_SRCS
   conf/pdfsettingswidget.ui
)

kconfig_add_kcfg_files(okularGenerator_poppler_PART_SRCS GENERATE_MOC conf/pdfsettings.kcfgc )

okular_add_generator(okularGenerator_poppler ${okularGenerator_poppler_PART_SRCS})

set(MENGSHEE_POPPLER_PRIVATE_BUILD_DIR "${CMAKE_BINARY_DIR}/../thirdparty/poppler-custom" CACHE PATH "Build directory of the matching local Poppler fork (generated private headers)")
if(NOT EXISTS "${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}/config.h")
    set(MENGSHEE_POPPLER_PRIVATE_BUILD_DIR "${CMAKE_BINARY_DIR}/../poppler-local")
endif()

add_library(pdf_page_sequence_editor STATIC
    ${CMAKE_SOURCE_DIR}/external/poppler/utils/PdfPageSequenceEditor.cc
    ${CMAKE_SOURCE_DIR}/external/poppler/utils/PdfPageSequenceEditor.h
)
set_target_properties(pdf_page_sequence_editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pdf_page_sequence_editor
    PUBLIC
        ${CMAKE_SOURCE_DIR}/external/poppler/utils
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/poppler
        ${CMAKE_SOURCE_DIR}/external/poppler/poppler
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}/poppler
)
target_link_libraries(pdf_page_sequence_editor Poppler::Core)

add_library(pdf_annotation_flattener STATIC
    ${CMAKE_SOURCE_DIR}/external/poppler/utils/PdfAnnotationFlattener.cc
    ${CMAKE_SOURCE_DIR}/external/poppler/utils/PdfAnnotationFlattener.h
)
set_target_properties(pdf_annotation_flattener PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pdf_annotation_flattener
    PUBLIC ${CMAKE_SOURCE_DIR}/external/poppler/utils
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/poppler
        ${CMAKE_SOURCE_DIR}/external/poppler/poppler
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}/poppler
)
target_link_libraries(pdf_annotation_flattener Poppler::Core)
target_link_libraries(okularGenerator_poppler pdf_annotation_flattener)

# These adapters and appearance policies belong to Mengshee, not the backend fork.
add_library(latex_appearance STATIC latexappearance.cpp latexappearance.h popplercorebridge.h)
set_target_properties(latex_appearance PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(latex_appearance
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/poppler
        ${CMAKE_SOURCE_DIR}/external/poppler/poppler
        ${CMAKE_SOURCE_DIR}/external/poppler/qt6/src
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}/poppler
)
target_link_libraries(latex_appearance PUBLIC Poppler::Qt6 PRIVATE Poppler::Core Qt6::Gui)
target_link_libraries(okularGenerator_poppler latex_appearance)

add_library(pdf_reading_views STATIC pdfreadingviews.cpp pdfreadingviews.h)
set_target_properties(pdf_reading_views PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(pdf_reading_views
    PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/poppler
        ${CMAKE_SOURCE_DIR}/external/poppler/poppler
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}
        ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}/poppler
)
target_link_libraries(pdf_reading_views PUBLIC okularcore Qt6::Core Qt6::Gui PRIVATE Poppler::Core)
target_link_libraries(okularGenerator_poppler pdf_reading_views)

target_include_directories(okularGenerator_poppler PRIVATE
    ${CMAKE_SOURCE_DIR}/external/poppler
    ${CMAKE_SOURCE_DIR}/external/poppler/poppler
    ${CMAKE_SOURCE_DIR}/external/poppler/qt6/src
    ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}
    ${MENGSHEE_POPPLER_PRIVATE_BUILD_DIR}/poppler
)
target_link_libraries(okularGenerator_poppler okularcore KF6::I18n KF6::Completion KF6::KIOWidgets Poppler::Core Poppler::Qt6 Qt6::Xml pdf_page_sequence_editor)
if(HAVE_TESSERACT)
    target_compile_definitions(okularGenerator_poppler PRIVATE HAVE_TESSERACT=1)
    target_link_libraries(okularGenerator_poppler Tesseract::Tesseract)
endif()
if(POPPLER_QT6_HAS_ANNOTATION_CUSTOM_SCALAR_PROPERTIES)
    target_compile_definitions(okularGenerator_poppler PRIVATE POPPLER_QT6_HAS_ANNOTATION_CUSTOM_SCALAR_PROPERTIES)
endif()

# Phase-1 engine module bridge: the plugin DLL also exports the module ABI.
# A copy under the MengsheePdfEngine* name lets the registry discover it while
# the legacy plugin path keeps working during the transition.
target_include_directories(okularGenerator_poppler PRIVATE ${CMAKE_SOURCE_DIR}/generators/pdf)
target_compile_definitions(okularGenerator_poppler PRIVATE MENGSHEE_PDF_ENGINE_MODULE_BUILD=1)
add_custom_command(TARGET okularGenerator_poppler POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:okularGenerator_poppler>"
        "${CMAKE_BINARY_DIR}/bin/MengsheePdfEnginePoppler.dll"
    COMMENT "Staging the Poppler engine module copy"
)

########### autotests ###############

if(BUILD_TESTING)
    ecm_add_test(autotests/testimagescaling.cpp
        TEST_NAME "imageScalingTest"
        LINK_LIBRARIES Qt6::Test Qt6::Gui
    )

    add_executable(poppler_engine_module_test autotests/enginemoduletest.cpp)
    # KDECMakeSettings sets CMAKE_WIN32_EXECUTABLE=ON, which turns every test
    # into a GUI-subsystem binary: stdout/stderr are then detached from the
    # console and CTest reports a failure with no output at all. Keep the
    # tests console programs so their QTest report is visible.
    set_target_properties(poppler_engine_module_test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(poppler_engine_module_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore)
    target_compile_definitions(poppler_engine_module_test PRIVATE
        MENGSHEE_POPPLER_ENGINE_PATH="${CMAKE_BINARY_DIR}/bin/MengsheePdfEnginePoppler.dll"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    add_dependencies(poppler_engine_module_test okularGenerator_poppler)
    add_test(NAME poppler_engine_module_test COMMAND poppler_engine_module_test)
    set_tests_properties(poppler_engine_module_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(poppler_plugin_test autotests/plugintest.cpp)
    set_target_properties(poppler_plugin_test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(poppler_plugin_test PRIVATE Qt6::Test okularcore KF6::CoreAddons)
    target_compile_definitions(poppler_plugin_test PRIVATE
        MENGSHEE_POPPLER_ENGINE_PATH="${CMAKE_BINARY_DIR}/bin/MengsheePdfEnginePoppler.dll"
        MENGSHEE_PDF_PLUGIN_PATH="$<TARGET_FILE:okularGenerator_mengshee_pdf>"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    add_dependencies(poppler_plugin_test okularGenerator_poppler okularGenerator_mengshee_pdf)
    add_test(NAME poppler_plugin_test COMMAND poppler_plugin_test)
    set_tests_properties(poppler_plugin_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Narrow PdfDocument read path of the real Poppler engine module.
    add_executable(poppler_narrow_document_test autotests/narrowdocumenttest.cpp)
    set_target_properties(poppler_narrow_document_test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(poppler_narrow_document_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore)
    target_compile_definitions(poppler_narrow_document_test PRIVATE
        MENGSHEE_POPPLER_ENGINE_PATH="${CMAKE_BINARY_DIR}/bin/MengsheePdfEnginePoppler.dll"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
        MENGSHEE_FORM_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/formSamples.pdf"
        MENGSHEE_ANNOTATION_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file1.pdf"
        MENGSHEE_SIGNATURE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/pdf_with_signature.pdf"
    )
    add_dependencies(poppler_narrow_document_test okularGenerator_poppler)
    add_test(NAME poppler_narrow_document_test COMMAND poppler_narrow_document_test)
    set_tests_properties(poppler_narrow_document_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Write-side capability contract (PdfSaver/PdfPrinter/PdfSigner) of the
    # real Poppler engine module, driven through PdfGeneratorBase.
    add_executable(poppler_capability_test autotests/capabilitytest.cpp)
    set_target_properties(poppler_capability_test PROPERTIES WIN32_EXECUTABLE FALSE)
    # settings_core.h is generated in the build root; the viewer document the
    # link test opens needs Okular::SettingsCore.
    target_include_directories(poppler_capability_test PRIVATE "${CMAKE_BINARY_DIR}")
    target_link_libraries(poppler_capability_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore Qt6::Widgets)
    target_compile_definitions(poppler_capability_test PRIVATE
        MENGSHEE_POPPLER_ENGINE_PATH="${CMAKE_BINARY_DIR}/bin/MengsheePdfEnginePoppler.dll"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    # The link capability resolves its page through the wrapper document of a
    # real viewer document, which the single PDF plugin opens with this module.
    add_dependencies(poppler_capability_test okularGenerator_poppler okularGenerator_mengshee_pdf)
    add_test(NAME poppler_capability_test COMMAND poppler_capability_test)
    set_tests_properties(poppler_capability_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

########### install files ###############
if (BUILD_DESKTOP)
  install( PROGRAMS okularApplication_pdf.desktop DESTINATION ${KDE_INSTALL_APPDIR} )
endif()
install( FILES org.kde.okular-poppler.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
