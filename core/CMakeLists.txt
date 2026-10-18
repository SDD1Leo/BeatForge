# project(JUCE ...) sets JUCE_VERSION only inside the JUCE directory scope.
get_directory_property(JUCE_VERSION DIRECTORY "${PROJECT_SOURCE_DIR}/external/JUCE" DEFINITION JUCE_VERSION)

configure_file(src/BuildInfo.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp" @ONLY)

# Pure C++ (no JUCE): the real-time engine pieces stay testable without a
# device or a window.
add_library(beatforge_core STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/BuildInfo.cpp"
    src/api/CommandApi.cpp
    src/audio/AudioEngine.cpp
    src/audio/DrumKit.cpp
    src/audio/DrumMachine.cpp
    src/audio/LevelMeter.cpp
    src/audio/Metronome.cpp
    src/audio/InstrumentPresets.cpp
    src/audio/Instruments.cpp
    src/audio/WavFile.cpp
    src/dsp/Biquad.cpp
    src/ipc/IpcServer.cpp
    $<IF:$<BOOL:${WIN32}>,src/ipc/IpcServer_windows.cpp,src/ipc/IpcServer_posix.cpp>
    src/midi/MidiFile.cpp
    src/midi/Note.cpp
    src/project/AudioExport.cpp
    src/project/MidiExport.cpp
    src/project/ScheduleBuilder.cpp
    src/project/Project.cpp
    src/project/ProjectFile.cpp
    src/project/Session.cpp
    src/project/TakeRecorder.cpp
    src/time/MusicalTime.cpp
    src/time/TempoMap.cpp
    src/time/Transport.cpp)

target_include_directories(beatforge_core PUBLIC include)
# JSON is part of the command API (api/CommandApi.h), so users of core see it too.
target_link_libraries(beatforge_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(beatforge_core PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(beatforge_core PRIVATE /W4 /permissive-)
    # strerror/getenv are fine here; NOMINMAX keeps <windows.h> from breaking std::min/max.
    target_compile_definitions(beatforge_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_link_libraries(beatforge_core PRIVATE advapi32)
else()
    target_compile_options(beatforge_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
