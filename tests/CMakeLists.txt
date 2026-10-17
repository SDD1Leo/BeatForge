include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(beatforge_tests
    unit/AllocationGuard.cpp
    unit/AudioEngineTests.cpp
    unit/BuildInfoTests.cpp
    unit/LevelMeterTests.cpp
    unit/TestToneTests.cpp)

target_link_libraries(beatforge_tests PRIVATE beatforge_core Catch2::Catch2WithMain)
target_compile_definitions(beatforge_tests PRIVATE BEATFORGE_EXPECTED_VERSION="${PROJECT_VERSION}")

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(beatforge_tests)

# Launches the real GUI app and checks that the window appears and the app exits
# cleanly. Uses xvfb-run when available so it also works without a desktop.
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    set(SMOKE_LAUNCHER ${XVFB_RUN} -a)
endif()

add_test(NAME app_smoke_launch
    COMMAND ${SMOKE_LAUNCHER} $<TARGET_FILE:BeatForge> --smoke-test)
set_tests_properties(app_smoke_launch PROPERTIES
    TIMEOUT 30
    PASS_REGULAR_EXPRESSION "SMOKE: window shown.*BeatForge shut down cleanly"
    FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|leaked objects")
