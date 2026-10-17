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
    unit/DrumKitTests.cpp
    unit/DrumMachineTests.cpp
    unit/LevelMeterTests.cpp
    unit/MetronomeTests.cpp
    unit/MidiSessionTests.cpp
    unit/MidiTests.cpp
    unit/MusicalTimeTests.cpp
    unit/ProjectFileTests.cpp
    unit/SessionTests.cpp
    unit/TempoMapTests.cpp
    unit/TransportTests.cpp)

target_link_libraries(beatforge_tests PRIVATE beatforge_core nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
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

# Opening project files through the real app (results are printed in smoke mode).
set(FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
set(OPEN_CASES
    "house-demo|SMOKE: project \"House Demo\" loaded with 0 warning"
    "needs-corrections|SMOKE: project \"Too Fast\" loaded with 1 warning.*Tempo 500 BPM is outside 40-240"
    "newer-version|SMOKE: Could not open project: .*newer BeatForge \\(file format version 7\\)"
    "truncated|SMOKE: Could not open project: .*not valid JSON")
foreach(case IN LISTS OPEN_CASES)
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 name)
    list(GET parts 1 expected)
    add_test(NAME app_open_${name}
        COMMAND ${SMOKE_LAUNCHER} $<TARGET_FILE:BeatForge> --smoke-test "${FIXTURES}/${name}.beatforge")
    set_tests_properties(app_open_${name} PROPERTIES
        TIMEOUT 30
        PASS_REGULAR_EXPRESSION "${expected}"
        FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|leaked objects")
endforeach()
