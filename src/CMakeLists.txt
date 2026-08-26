# Main d3d11.dll proxy - must be ultra-lightweight at load time.
# Heavy dependencies (Tesseract, sqlite3) live in bronco_ocr.dll and are loaded
# at runtime via LoadLibrary when the pipeline worker thread needs OCR.
#
# With x64-windows-static-md triplet, all vcpkg libraries (imgui, nlohmann-json)
# are linked statically into this DLL. Only Windows system DLLs are needed at runtime.

set(BRONCO_SOURCES
    dllmain.cpp
    proxy/d3d11_proxy.cpp
    hook/present_hook.cpp
    overlay/imgui_overlay.cpp
    ocr/ocr_loader.cpp
    config/config.cpp
    pipeline/pipeline.cpp
)

# Build as a shared library (DLL on Windows)
add_library(d3d11 SHARED
    ${BRONCO_SOURCES}
    proxy/d3d11_exports.def
    bronco.rc
)

# Ensure dynamic CRT (/MD) - consistent with x64-windows-static-md triplet
set_target_properties(d3d11 PROPERTIES
    OUTPUT_NAME "d3d11"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
)

# Include directories
target_include_directories(d3d11 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link ONLY lightweight/system dependencies.
# NO Tesseract, NO sqlite3 - those are in bronco_ocr.dll loaded at runtime.
# imgui and nlohmann-json are linked statically (embedded in this DLL).
target_link_libraries(d3d11 PRIVATE
    imgui::imgui
    nlohmann_json::nlohmann_json
    unofficial::minhook::minhook
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
