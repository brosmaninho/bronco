cmake_minimum_required(VERSION 3.21)
project(bronco VERSION 0.1.0 LANGUAGES CXX)

# C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output DLL name as d3d11.dll
set(CMAKE_SHARED_LIBRARY_PREFIX "")

# Find dependencies via vcpkg
find_package(imgui CONFIG REQUIRED)
find_package(Tesseract CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Add source subdirectory
add_subdirectory(src)
