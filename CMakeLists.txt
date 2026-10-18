cmake_minimum_required(VERSION 3.22)

project(BeatForge VERSION 0.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BEATFORGE_BUILD_TESTS "Build BeatForge tests" ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE submodule missing. Run: git submodule update --init --depth 1")
endif()

add_subdirectory(external/JUCE EXCLUDE_FROM_ALL)

# JSON for the project file format (header-only, used inside core only).
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# GUI-free code (project model, transport, engine, ...) lives in core so it can
# be unit tested and later driven by the command API without a window.
add_subdirectory(core)
add_subdirectory(app)

if(BEATFORGE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
