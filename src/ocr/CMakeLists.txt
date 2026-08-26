# bronco_ocr.dll - Separate DLL containing heavy OCR and translation dependencies.
# This is loaded at runtime via LoadLibrary by the main d3d11.dll proxy, so that
# the proxy itself has zero external dependencies at load time.

set(BRONCO_OCR_SOURCES
    ocr_engine.cpp
    ocr_api.cpp
    ../translation/translator.cpp
    ../translation/dictionary.cpp
)

add_library(bronco_ocr SHARED
    ${BRONCO_OCR_SOURCES}
)

set_target_properties(bronco_ocr PROPERTIES
    OUTPUT_NAME "bronco_ocr"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_include_directories(bronco_ocr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_definitions(bronco_ocr PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    BRONCO_OCR_EXPORTS
)

target_link_libraries(bronco_ocr PRIVATE
    Tesseract::libtesseract
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
)
