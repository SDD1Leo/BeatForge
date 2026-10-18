#pragma once

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include "ui/Selection.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <set>

namespace beatforge
{
    // NEBULA piano roll for the synth track's MIDI clip.
    //
    //  - Click empty grid: add a note (snapped to the grid) and hear it.
    //  - Drag empty grid: box-select. Shift/Ctrl+click: add to selection.
    //  - Drag a note: move (time snaps to the grid, pitch by rows).
    //  - Drag a note's right edge: resize. Right-click a note: delete it.
    //  - Keys: Delete/Backspace, Ctrl+A, Escape, Up/Down (Shift = octave),
    //    Left/Right (by grid), Q quantize (selection, or all).
    //  - Velocity lane: drag bars up/down (moves all selected together).
    //  - Click the keyboard to hear a pitch.
    // All edits go through the Session.
    class PianoRollPanel final : public juce::Component,
                                 private juce::Timer
    {
    public:
        PianoRollPanel (audio::AudioEngine&, project::Session&, Selection&);
        ~PianoRollPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int lowestPitch = 24, highestPitch = 108;   // C1..C8
        static constexpr int rowHeight = 13, keyboardWidth = 58;

    private:
        class Editor;
        class VelocityLane;

        void timerCallback() override;
        void refreshFromSession();
        void quantizeSelectionOrAll();
        time::Tick gridTicks() const;

        // What the panel edits: the selected synth track's selected clip.
        project::TrackId trackId() const;
        project::ClipId clipId() const;
        const project::Clip* clipPtr() const;
        int slot() const;

        audio::AudioEngine& engine;
        project::Session& session;
        Selection& target;
        int listenerId = 0, targetListener = 0;
        project::ClipId shownClip = 0;

        std::set<midi::NoteId> selection;
        time::Tick newNoteLength = 240;

        juce::ComboBox clipLengthBox, gridBox;
        juce::TextButton quantizeButton { "Quantize" }, createButton { "Create a 4-bar clip" };
        juce::Viewport viewport;
        std::unique_ptr<Editor> editor;
        std::unique_ptr<VelocityLane> velocityLane;
        juce::Rectangle<int> headerArea, emptyArea;
        bool scrolledToMiddle = false;
        double lastPlayhead = -1.0;
    };
}
