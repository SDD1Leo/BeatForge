#pragma once

#include <beatforge/audio/AudioEngine.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace beatforge
{
    // The drum machine: one row per drum (name, mute, solo, volume) and a
    // 16-step grid. Click or drag across steps to paint them; while stopped a
    // newly enabled step is auditioned. The playing step is highlighted.
    class DrumMachinePanel final : public juce::Component,
                                   private juce::Timer
    {
    public:
        explicit DrumMachinePanel (audio::AudioEngine&);
        ~DrumMachinePanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static juce::Colour laneColour (int lane);
        static constexpr int preferredHeight = 44 + audio::DrumMachine::numLanes * 44 + 12;

    private:
        class StepGrid;
        class LaneHeader;

        void timerCallback() override;

        audio::AudioEngine& engine;
        juce::TextButton clearButton { "Clear" };
        std::unique_ptr<StepGrid> grid;
        std::array<std::unique_ptr<LaneHeader>, audio::DrumMachine::numLanes> headers;
        juce::Rectangle<int> titleArea;

        std::array<std::uint32_t, audio::DrumMachine::numLanes> lastTriggerCounts {};
        std::array<float, audio::DrumMachine::numLanes> flashes {};
    };
}
