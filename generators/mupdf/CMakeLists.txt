set(mupdf_backend_sources generator_mupdf.cpp annots.cpp latexappearance.cpp latexappearancepolicy.cpp pageediting.cpp pageimport.cpp metadata.cpp navigation.cpp linkediting.cpp ocr.cpp export.cpp forms.cpp resources.cpp)
if(BUILD_DESKTOP)
    install(PROGRAMS ../poppler/okularApplication_pdf.desktop DESTINATION ${KDE_INSTALL_APPDIR})
endif()
# Keep the engine and its embedded resources in one DLL. Both the plugin and
# the UI's PDF helpers use this runtime rather than linking MuPDF statically.
add_library(mengshee_pdf_runtime SHARED ${mupdf_backend_sources} printing.cpp vectorprintdevice.cpp runtimehelpers.cpp systemfonts.cpp context.cpp)
generate_export_header(mengshee_pdf_runtime)
set_target_properties(mengshee_pdf_runtime PROPERTIES OUTPUT_NAME MengsheePdfRuntime)
target_include_directories(mengshee_pdf_runtime PUBLIC "${CMAKE_CURRENT_BINARY_DIR}" "${MuPDF_INCLUDE_DIR}")
target_compile_definitions(mengshee_pdf_runtime PUBLIC MENGSHEE_USE_MUPDF=1)
target_link_libraries(mengshee_pdf_runtime PRIVATE okularcore MuPDF::MuPDF Qt6::Gui Qt6::Xml Qt6::PrintSupport KF6::I18n)
if(WIN32)
    target_link_libraries(mengshee_pdf_runtime PRIVATE dwrite)
endif()
install(TARGETS mengshee_pdf_runtime RUNTIME DESTINATION bin LIBRARY DESTINATION ${KDE_INSTALL_LIBDIR})
okular_add_generator(okularGenerator_mupdf plugin.cpp)
target_link_libraries(okularGenerator_mupdf PRIVATE mengshee_pdf_runtime okularcore)

# Phase-1 engine module: reuses MuPDFGenerator unchanged and exports the
# dynamic module ABI. It is not installed or loaded by the application yet.
add_library(mengshee_pdf_engine_mupdf SHARED mupdfmodule.cpp)
target_compile_definitions(mengshee_pdf_engine_mupdf PRIVATE MENGSHEE_PDF_ENGINE_MODULE_BUILD=1)
target_link_libraries(mengshee_pdf_engine_mupdf PRIVATE mengshee_pdf_frontend mengshee_pdf_runtime)
set_target_properties(mengshee_pdf_engine_mupdf PROPERTIES
    OUTPUT_NAME MengsheePdfEngineMuPdf
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(BUILD_TESTING)
    add_executable(mupdf_engine_module_test autotests/enginemoduletest.cpp)
    target_link_libraries(mupdf_engine_module_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore)
    target_compile_definitions(mupdf_engine_module_test PRIVATE
        MENGSHEE_MUPDF_ENGINE_DIR="$<TARGET_FILE_DIR:mengshee_pdf_engine_mupdf>"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    add_dependencies(mupdf_engine_module_test mengshee_pdf_engine_mupdf)
    add_test(NAME mupdf_engine_module_test COMMAND mupdf_engine_module_test)
    set_tests_properties(mupdf_engine_module_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(mupdf_generator_base_test autotests/generatorbasetest.cpp)
    # Keep the test a console program so its QTest report is visible.
    set_target_properties(mupdf_generator_base_test PROPERTIES WIN32_EXECUTABLE FALSE)
    target_link_libraries(mupdf_generator_base_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore)
    target_compile_definitions(mupdf_generator_base_test PRIVATE
        MENGSHEE_MUPDF_ENGINE_DIR="$<TARGET_FILE_DIR:mengshee_pdf_engine_mupdf>"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    add_dependencies(mupdf_generator_base_test mengshee_pdf_engine_mupdf)
    add_test(NAME mupdf_generator_base_test COMMAND mupdf_generator_base_test)
    set_tests_properties(mupdf_generator_base_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(mupdf_plugin_test autotests/plugintest.cpp)
    target_link_libraries(mupdf_plugin_test PRIVATE Qt6::Test okularcore KF6::CoreAddons)
    target_compile_definitions(mupdf_plugin_test PRIVATE
        MENGSHEE_MUPDF_ENGINE_DIR="$<TARGET_FILE_DIR:mengshee_pdf_engine_mupdf>"
        MENGSHEE_PDF_PLUGIN_PATH="$<TARGET_FILE:okularGenerator_mengshee_pdf>"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    add_dependencies(mupdf_plugin_test mengshee_pdf_engine_mupdf okularGenerator_mengshee_pdf)
    add_test(NAME mupdf_plugin_test COMMAND mupdf_plugin_test)
    set_tests_properties(mupdf_plugin_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Narrow PdfDocument read path of the real MuPDF engine module.
    add_executable(mupdf_narrow_document_test autotests/narrowdocumenttest.cpp)
    target_link_libraries(mupdf_narrow_document_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore)
    target_compile_definitions(mupdf_narrow_document_test PRIVATE
        MENGSHEE_MUPDF_ENGINE_DIR="$<TARGET_FILE_DIR:mengshee_pdf_engine_mupdf>"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    add_dependencies(mupdf_narrow_document_test mengshee_pdf_engine_mupdf)
    add_test(NAME mupdf_narrow_document_test COMMAND mupdf_narrow_document_test)
    set_tests_properties(mupdf_narrow_document_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Write-side capability contract (PdfSaver/PdfPrinter/PdfSigner) of the
    # real MuPDF engine module, driven through PdfGeneratorBase.
    add_executable(mupdf_capability_test autotests/capabilitytest.cpp)
    # Keep the test a console program so its QTest report is visible even when
    # KDECMakeSettings turns executables into GUI-subsystem binaries.
    set_target_properties(mupdf_capability_test PROPERTIES WIN32_EXECUTABLE FALSE)
    # settings_core.h is generated in the build root; the viewer document the
    # link test opens needs Okular::SettingsCore.
    target_include_directories(mupdf_capability_test PRIVATE "${CMAKE_BINARY_DIR}")
    target_link_libraries(mupdf_capability_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore Qt6::Widgets)
    target_compile_definitions(mupdf_capability_test PRIVATE
        MENGSHEE_MUPDF_ENGINE_DIR="$<TARGET_FILE_DIR:mengshee_pdf_engine_mupdf>"
        MENGSHEE_TEST_PDF="${CMAKE_SOURCE_DIR}/autotests/data/file2.pdf"
    )
    # The link capability resolves its page through the wrapper document of a
    # real viewer document, which the single PDF plugin opens with this module.
    add_dependencies(mupdf_capability_test mengshee_pdf_engine_mupdf okularGenerator_mengshee_pdf)
    add_test(NAME mupdf_capability_test COMMAND mupdf_capability_test)
    set_tests_properties(mupdf_capability_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

add_executable(mupdf_backend_test EXCLUDE_FROM_ALL autotests/backendtest.cpp)
if(BUILD_TESTING)
    set_target_properties(mupdf_backend_test PROPERTIES EXCLUDE_FROM_ALL FALSE)
    add_test(NAME mupdf_backend_test COMMAND mupdf_backend_test)
    set_tests_properties(mupdf_backend_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
target_link_libraries(mupdf_backend_test PRIVATE okularcore mengshee_pdf_runtime MuPDF::MuPDF Qt6::Test Qt6::Gui Qt6::Xml Qt6::PrintSupport KF6::I18n)
if(HAVE_TESSERACT)
    foreach(target IN ITEMS mengshee_pdf_runtime mupdf_backend_test)
        target_compile_definitions(${target} PRIVATE HAVE_TESSERACT=1)
        if(WIN32)
            # Upstream's MSVC archive already embeds Tesseract. Use its matching
            # public headers and avoid importing the SDK's second implementation.
            target_include_directories(${target} PRIVATE "${MuPDF_INCLUDE_DIR}/mupdf-thirdparty")
        else()
            target_link_libraries(${target} PRIVATE Tesseract::Tesseract)
        endif()
    endforeach()
endif()

# Opt-in benchmark: exercise the production shared runtime without a second MuPDF copy.
add_executable(mupdf_render_benchmark EXCLUDE_FROM_ALL autotests/renderbenchmark.cpp)
add_executable(mupdf_print_test EXCLUDE_FROM_ALL autotests/printtest.cpp)
target_link_libraries(mupdf_print_test PRIVATE mengshee_pdf_runtime MuPDF::MuPDF okularcore Qt6::Widgets Qt6::PrintSupport Qt6::Test)
if(BUILD_TESTING)
    set_target_properties(mupdf_print_test PROPERTIES EXCLUDE_FROM_ALL FALSE)
    add_test(NAME mupdf_print_test COMMAND mupdf_print_test)
    set_tests_properties(mupdf_print_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
target_link_libraries(mupdf_render_benchmark PRIVATE mengshee_pdf_runtime okularcore Qt6::Widgets)
if(WIN32)
    target_link_libraries(mupdf_render_benchmark PRIVATE psapi)
endif()

add_executable(mupdf_page_lifecycle_test EXCLUDE_FROM_ALL autotests/pagelifecycletest.cpp)
target_link_libraries(mupdf_page_lifecycle_test PRIVATE okularcore Qt6::Widgets Qt6::Test)
add_dependencies(mupdf_page_lifecycle_test okularGenerator_mupdf)

add_executable(mupdf_latex_rotation_test EXCLUDE_FROM_ALL autotests/latexrotationtest.cpp)
target_link_libraries(mupdf_latex_rotation_test PRIVATE MuPDF::MuPDF mengshee_pdf_runtime okularcore Qt6::Widgets Qt6::Test)
add_dependencies(mupdf_latex_rotation_test okularGenerator_mupdf)
if(BUILD_TESTING)
    add_test(NAME mupdf_latex_rotation_test COMMAND mupdf_latex_rotation_test)
    set_target_properties(mupdf_latex_rotation_test PROPERTIES EXCLUDE_FROM_ALL FALSE)
endif()

add_executable(mupdf_rotation_ui_test EXCLUDE_FROM_ALL autotests/rotationuitest.cpp)
target_link_libraries(mupdf_rotation_ui_test PRIVATE mengshee_pdf_runtime okularpart okularcore Qt6::Widgets Qt6::Test KF6::ConfigCore KF6::Parts)
add_dependencies(mupdf_rotation_ui_test okularGenerator_mupdf)

add_executable(mupdf_ai_page_test EXCLUDE_FROM_ALL autotests/aipageanalysistest.cpp)
target_link_libraries(mupdf_ai_page_test PRIVATE okularpart okularcore Qt6::Widgets Qt6::Network Qt6::Test KF6::ConfigCore KF6::Parts)
add_dependencies(mupdf_ai_page_test okularGenerator_mupdf)
if(BUILD_TESTING)
    add_test(NAME mupdf_ai_page_test COMMAND mupdf_ai_page_test)
    set_target_properties(mupdf_ai_page_test PROPERTIES EXCLUDE_FROM_ALL FALSE)
    set_tests_properties(mupdf_ai_page_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
