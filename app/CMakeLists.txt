juce_add_gui_app(BeatForge
    PRODUCT_NAME "BeatForge"
    COMPANY_NAME "BeatForge"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(BeatForge
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/ui/BeatForgeLookAndFeel.cpp)

target_include_directories(BeatForge PRIVATE Source)

target_compile_definitions(BeatForge
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:BeatForge,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:BeatForge,JUCE_VERSION>")

target_link_libraries(BeatForge
    PRIVATE
        beatforge_core
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
