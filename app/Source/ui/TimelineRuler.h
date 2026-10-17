#pragma once

#include <beatforge/audio/AudioEngine.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // Read-only bar ruler: bar numbers, beat ticks, the loop region and the
    // playhead. Becomes the top of the arrangement timeline in a later phase.
    class TimelineRuler final : public juce::Component,
                                private juce::Timer
    {
    public:
        explicit TimelineRuler (audio::AudioEngine&);

        void setLoop (bool enabled, int bars);

        void paint (juce::Graphics&) override;

    private:
        void timerCallback() override;
        float tickToX (double tick, double ticksPerBar) const;
        int visibleBars() const;
        int firstVisibleBar (double ticksPerBar) const;   // 0-based; pages with the playhead

        audio::AudioEngine& engine;
        bool loopOn = false;
        int loopBars = 4;
        double lastTicks = -1.0;
    };
}
