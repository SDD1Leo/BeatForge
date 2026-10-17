# project(JUCE ...) sets JUCE_VERSION only inside the JUCE directory scope.
get_directory_property(JUCE_VERSION DIRECTORY "${PROJECT_SOURCE_DIR}/external/JUCE" DEFINITION JUCE_VERSION)

configure_file(src/BuildInfo.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp" @ONLY)

add_library(beatforge_core STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp")

target_include_directories(beatforge_core PUBLIC include)
target_compile_features(beatforge_core PUBLIC cxx_std_20)
target_compile_options(beatforge_core PRIVATE -Wall -Wextra -Wpedantic)
