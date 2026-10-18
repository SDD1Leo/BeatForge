#pragma once

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include "ui/EditorPage.h"
#include "ui/Selection.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace beatforge
{
    // The drum machine: one row per drum (name, mute, solo, volume) and a
    // 16-step grid. Click or drag across steps to paint them; while stopped a
    // newly enabled step is auditioned. The playing step is highlighted.
    // Double-click a drum name to rename it. Edits go through the Session.
    class DrumMachinePanel final : public EditorPage,
                                   private juce::Timer
    {
    public:
        DrumMachinePanel (audio::AudioEngine&, project::Session&, Selection&);
        ~DrumMachinePanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static juce::Colour laneColour (int lane);

        std::function<void()> onTextEditFinished;   // a rename finished: return focus
        static constexpr int preferredHeight = headerHeight + 14 + 18 + 6 + audio::DrumMachine::numLanes * 52 + 14;

    private:
        class StepGrid;
        class LaneHeader;

        void timerCallback() override;
        void refreshAll();

        // What the panel edits: the selected drum track and clip (0 when none).
        project::TrackId trackId() const;
        project::ClipId clipId() const;
        const project::Track* track() const;
        const project::Clip* clip() const;
        int slot() const;

        audio::AudioEngine& engine;
        project::Session& session;
        Selection& selection;
        int listenerId = 0, selectionListener = 0;
        juce::TextButton clearButton { "Clear" };
        std::unique_ptr<StepGrid> grid;
        std::array<std::unique_ptr<LaneHeader>, audio::DrumMachine::numLanes> headers;
        juce::Rectangle<int> stepNumbers, clipChip, headerDivider;

        std::array<std::uint32_t, audio::DrumMachine::numLanes> lastTriggerCounts {};
        std::array<float, audio::DrumMachine::numLanes> flashes {};
    };
}
