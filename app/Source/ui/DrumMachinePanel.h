#pragma once

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace beatforge
{
    // The drum machine: one row per drum (name, mute, solo, volume) and a
    // 16-step grid. Click or drag across steps to paint them; while stopped a
    // newly enabled step is auditioned. The playing step is highlighted.
    // Double-click a drum name to rename it. Edits go through the Session.
    class DrumMachinePanel final : public juce::Component,
                                   private juce::Timer
    {
    public:
        DrumMachinePanel (audio::AudioEngine&, project::Session&);
        ~DrumMachinePanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static juce::Colour laneColour (int lane);

        std::function<void()> onTextEditFinished;   // a rename finished: return focus
        static constexpr int preferredHeight = 44 + audio::DrumMachine::numLanes * 44 + 12;

    private:
        class StepGrid;
        class LaneHeader;

        void timerCallback() override;

        audio::AudioEngine& engine;
        project::Session& session;
        int listenerId = 0;
        juce::TextButton clearButton { "Clear" };
        std::unique_ptr<StepGrid> grid;
        std::array<std::unique_ptr<LaneHeader>, audio::DrumMachine::numLanes> headers;
        juce::Rectangle<int> titleArea;

        std::array<std::uint32_t, audio::DrumMachine::numLanes> lastTriggerCounts {};
        std::array<float, audio::DrumMachine::numLanes> flashes {};
    };
}
