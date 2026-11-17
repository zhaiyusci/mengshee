# Shared PDF front end: engine ABI/registry and neutral utilities.
# Phase 1: this library is not linked into the application yet.

add_library(mengshee_pdf_frontend STATIC
    pdfengine.h
    pdfenginemodule.h
    pdfengineregistry.h
    pdfengineregistry.cpp
    pdfgeneratorbase.h
    pdfgeneratorbase.cpp
    pdfutils.h
    pdfutils.cpp
)

target_include_directories(mengshee_pdf_frontend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mengshee_pdf_frontend
    PUBLIC
        Qt6::Core
        okularcore
)

# Single PDF plugin. Phase 1 builds it next to the legacy plugins so the module
# path can be exercised; it is not installed yet and nothing selects it.
add_library(okularGenerator_mengshee_pdf SHARED
    mengsheeplugin.cpp
    libokularGenerator_mengshee_pdf.json
)
target_link_libraries(okularGenerator_mengshee_pdf PRIVATE mengshee_pdf_frontend okularcore)
set_target_properties(okularGenerator_mengshee_pdf PROPERTIES
    PREFIX ""
    OUTPUT_NAME okularGenerator_mengshee_pdf
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/okular_generators"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/okular_generators"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if(BUILD_TESTING)
    # A tiny module that exercises the C ABI, discovery and ownership without
    # pulling in a real PDF backend.
    add_library(mengshee_pdf_engine_fake SHARED autotests/fakeengine.cpp)
    target_compile_definitions(mengshee_pdf_engine_fake PRIVATE MENGSHEE_PDF_ENGINE_MODULE_BUILD=1)
    target_include_directories(mengshee_pdf_engine_fake PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mengshee_pdf_engine_fake PRIVATE Qt6::Core okularcore)
    set_target_properties(mengshee_pdf_engine_fake PROPERTIES
        OUTPUT_NAME MengsheePdfEngineFake
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    add_executable(mengshee_pdf_registry_test autotests/registrytest.cpp)
    target_link_libraries(mengshee_pdf_registry_test PRIVATE mengshee_pdf_frontend Qt6::Test okularcore)
    target_compile_definitions(mengshee_pdf_registry_test PRIVATE
        FAKE_ENGINE_DIR="$<TARGET_FILE_DIR:mengshee_pdf_engine_fake>"
    )
    add_dependencies(mengshee_pdf_registry_test mengshee_pdf_engine_fake)
    add_test(NAME mengshee_pdf_registry_test COMMAND mengshee_pdf_registry_test)
    set_tests_properties(mengshee_pdf_registry_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
