juce_add_gui_app(BeatForge
    PRODUCT_NAME "BeatForge"
    COMPANY_NAME "BeatForge"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(BeatForge
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/audio/AudioDeviceController.cpp
        Source/input/ComputerKeyboard.cpp
        Source/ipc/AiBridge.cpp
        Source/project/Autosave.cpp
        Source/project/ProjectActions.cpp
        Source/ui/ArrangementView.cpp
        Source/ui/BeatForgeLookAndFeel.cpp
        Source/ui/DrumMachinePanel.cpp
        Source/ui/ExportAudioDialog.cpp
        Source/ui/InstrumentPanel.cpp
        Source/ui/Knob.cpp
        Source/ui/LevelMeterComponent.cpp
        Source/ui/MixerPanel.cpp
        Source/ui/PianoRollPanel.cpp
        Source/ui/ProjectBrowser.cpp
        Source/ui/ProjectNameField.cpp
        Source/ui/ShortcutsDialog.cpp
        Source/ui/SignatureField.cpp
        Source/ui/StatusBar.cpp
        Source/ui/TabBar.cpp
        Source/ui/TempoField.cpp
        Source/ui/TransportBar.cpp
        Source/ui/TransportButton.cpp
        Source/ui/UserGuideDialog.cpp)

target_include_directories(BeatForge PRIVATE Source)

target_compile_definitions(BeatForge
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        # Off for now: Ubuntu's libjack is jackd2's, so probing it without a
        # JACK server just prints connection errors. ALSA reaches PipeWire.
        JUCE_JACK=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BeatForge,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BeatForge,JUCE_VERSION>")

# The user guide and the UI fonts are built into the app, so Help always shows the
# matching version and every machine draws the same typefaces.
juce_add_binary_data(BeatForgeDocs SOURCES
    "${PROJECT_SOURCE_DIR}/USER_GUIDE.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/Inter-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/Inter-SemiBold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/Inter-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/JetBrainsMono-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/JetBrainsMono-Medium.ttf")

target_link_libraries(BeatForge
    PRIVATE
        beatforge_core
        BeatForgeDocs
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
