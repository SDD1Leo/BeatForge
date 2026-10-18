#pragma once

#include "ui/Selection.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>

namespace beatforge
{
    // NEBULA arrangement: ruler (bars, loop region, seek), track headers
    // (name, mute, solo, volume, pan) and lanes with clips. All edits go
    // through the Session; drags are single undo steps.
    //
    //  Ruler:  drag in the top strip to set the loop; click/drag below to move the playhead.
    //  Lanes:  double-click empty space to create a clip; drag a clip to move it
    //          (snaps to beats, can move to another track of the same kind);
    //          drag its right edge to resize; double-click to edit it;
    //          right-click for Duplicate / Delete; Ctrl+D, Delete on the selected clip.
    //  Headers: double-click the name to rename; right-click for Rename / Delete.
    class ArrangementView final : public juce::Component,
                                  private juce::Timer
    {
    public:
        ArrangementView (audio::AudioEngine&, project::Session&, Selection&);
        ~ArrangementView() override;

        std::function<void()> onOpenEditor;       // a clip was double-clicked
        std::function<void()> onTextEditFinished; // a rename finished: return focus

        void duplicateSelectedClip();
        void deleteSelectedClip();

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int headerWidth = 236, rowHeight = 60, rulerHeight = 32;

    private:
        class Ruler;
        class Lanes;
        class TrackHeader;

        void timerCallback() override;
        void rebuild();                    // tracks added/removed/reordered
        void refresh();                    // values changed
        int barsShown() const;

        audio::AudioEngine& engine;
        project::Session& session;
        Selection& selection;
        int sessionListener = 0, selectionListener = 0;

        std::unique_ptr<Ruler> ruler;
        std::unique_ptr<Lanes> lanes;
        juce::Viewport viewport;
        juce::TextButton addDrumsButton { "+ Drums" }, addSynthButton { "+ Synth" };
        std::vector<project::TrackId> shownTracks;
    };
}
