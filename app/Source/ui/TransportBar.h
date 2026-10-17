#pragma once

#include "ui/TempoField.h"
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
        void toggleLoop();
        void toggleMetronome();

        std::function<void()> onOpenAudioSettings;
        std::function<void (bool enabled, int bars)> onLoopChanged;
        std::function<void()> onTextEditFinished;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void refresh();
        void applyLoop();

        audio::AudioEngine& engine;
        TransportButton playButton, stopButton, loopButton, metronomeButton;
        TempoField tempoField;
        juce::ComboBox signatureBox, loopLengthBox;
        juce::TextButton audioSettingsButton { "Audio Settings..." };
        juce::Rectangle<int> positionArea, beatArea;

        juce::String barBeatText { "1.1.000" }, timeText { "00:00.000" };
        int currentBeat = 0;   // 1-based beat lit in the indicator, 0 = none
        bool deviceAvailable = false;
        bool loopOn = false;
    };
}
