cmake_minimum_required(VERSION 3.21)
project(bronco VERSION 0.1.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Use dynamic CRT (/MD) - required for x64-windows-static-md triplet consistency.
# All vcpkg libraries are linked statically, but CRT remains dynamic.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "MSVC runtime library")

# Output DLL name as d3d11.dll
set(CMAKE_SHARED_LIBRARY_PREFIX "")

# Find dependencies via vcpkg
find_package(imgui CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Build the OCR DLL first (has heavy dependencies: Tesseract, sqlite3)
add_subdirectory(src/ocr)

# Build the main d3d11.dll proxy (lightweight, no heavy dependencies)
add_subdirectory(src)
