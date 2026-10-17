#pragma once

#include "ui/TransportButton.h"

#include <beatforge/audio/AudioEngine.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    class TransportBar final : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit TransportBar (audio::AudioEngine&);

        // Play is only possible while an output device is open.
        void setDeviceAvailable (bool available);
        void togglePlayStop();

        std::function<void()> onOpenAudioSettings;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void refresh();

        audio::AudioEngine& engine;
        TransportButton playButton, stopButton;
        juce::TextButton audioSettingsButton { "Audio Settings..." };
        juce::Rectangle<int> timeArea, stateArea, toneArea;

        juce::String timeText { "00:00.000" };
        juce::String stateText { "STOPPED" };
        juce::Colour stateColour;
        bool deviceAvailable = false;
    };
}
