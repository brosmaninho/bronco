# bronco_ocr.dll - Separate DLL containing heavy OCR and translation dependencies.
# This is loaded at runtime via LoadLibrary by the main d3d11.dll proxy, so that
# the proxy itself has zero external dependencies at load time.
#
# With x64-windows-static-md triplet, all vcpkg libraries (Tesseract, leptonica,
# sqlite3, nlohmann-json) are linked statically into this DLL. Only Windows system
# DLLs are needed at runtime.

set(BRONCO_OCR_SOURCES
    ocr_engine.cpp
    ocr_api.cpp
    ../translation/translator.cpp
    ../translation/dictionary.cpp
)

add_library(bronco_ocr SHARED
    ${BRONCO_OCR_SOURCES}
)

# Ensure dynamic CRT (/MD) - consistent with x64-windows-static-md triplet
set_target_properties(bronco_ocr PROPERTIES
    OUTPUT_NAME "bronco_ocr"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
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

# All these libraries are linked statically (embedded in bronco_ocr.dll)
# thanks to the x64-windows-static-md triplet.
target_link_libraries(bronco_ocr PRIVATE
    Tesseract::libtesseract
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
)
