#pragma once

#include <beatforge/audio/LevelMeter.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace beatforge
{
    // Stereo peak meter with decay, peak-hold and a latching clip light.
    // Polls the engine's LevelMeter at UI frame rate; click to reset clip.
    class LevelMeterComponent final : public juce::Component,
                                      public juce::SettableTooltipClient,
                                      private juce::Timer
    {
    public:
        explicit LevelMeterComponent (audio::LevelMeter&);

        // Without the dB scale (narrow mixer strips).
        void setShowScale (bool shouldShow) { showScale = shouldShow; repaint(); }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr float minDb = -60.0f;

    private:
        void timerCallback() override;
        float dbToY (float db, juce::Rectangle<float> bar) const;

        struct Channel
        {
            float displayDb = minDb;
            float holdDb = minDb;
            double holdUntilMs = 0.0;
            bool clipped = false;
        };

        audio::LevelMeter& meter;
        std::array<Channel, 2> channels;
        double lastTickMs = 0.0;
        bool showScale = true;
    };
}
