# Collect all source files
set(BRONCO_SOURCES
    dllmain.cpp
    proxy/d3d11_proxy.cpp
    hook/present_hook.cpp
    overlay/imgui_overlay.cpp
    ocr/ocr_engine.cpp
    translation/translator.cpp
    translation/dictionary.cpp
    config/config.cpp
    pipeline/pipeline.cpp
)

# Build as a shared library (DLL on Windows)
add_library(d3d11 SHARED
    ${BRONCO_SOURCES}
    proxy/d3d11_exports.def
)

# Set output name to d3d11 (produces d3d11.dll)
set_target_properties(d3d11 PROPERTIES
    OUTPUT_NAME "d3d11"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Include directories
target_include_directories(d3d11 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies
target_link_libraries(d3d11 PRIVATE
    imgui::imgui
    Tesseract::libtesseract
    unofficial::sqlite3::sqlite3
    nlohmann_json::nlohmann_json
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
)

# Windows-specific definitions
target_compile_definitions(d3d11 PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)
