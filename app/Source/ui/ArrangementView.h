#pragma once

#include "ui/Selection.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>

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
    //  Headers: double-click the name to rename; drag up/down to reorder;
    //           right-click for Rename / Move up / Move down / Delete.
    //  View:   Ctrl+wheel (or the wheel over the ruler) zooms, Shift+wheel or the
    //          scroll bar scrolls; - / + / Fit below the headers. While playing,
    //          the view pages along with the playhead.
    class ArrangementView final : public juce::Component,
                                  private juce::Timer,
                                  private juce::ScrollBar::Listener
    {
    public:
        ArrangementView (audio::AudioEngine&, project::Session&, Selection&);
        ~ArrangementView() override;

        std::function<void()> onOpenEditor;       // a clip was double-clicked
        std::function<void()> onTextEditFinished; // a rename finished: return focus

        void duplicateSelectedClip();
        void deleteSelectedClip();
        // Clipboard (inside BeatForge): paste goes onto the selected track, at the playhead.
        bool copySelectedClip();
        void cutSelectedClip();
        bool pasteClip();
        std::function<void (const juce::String&)> onMessage;   // short feedback for the status bar

        void zoomIn();
        void zoomOut();
        void zoomToFit();     // the whole song

        void paint (juce::Graphics&) override;
        void resized() override;
        void paintOverChildren (juce::Graphics&) override;

        // Areas the studio tour points at.
        juce::Rectangle<int> getTrackListBounds() const { return getLocalBounds().withWidth (headerWidth); }
        juce::Rectangle<int> getTimelineBounds() const  { return getLocalBounds().withTrimmedLeft (headerWidth); }

        static constexpr int headerWidth = 264, rowHeight = 70, rulerHeight = 40, footerHeight = 40;
        static constexpr double minBarsVisible = 1.0, maxBarsVisible = 256.0;

    private:
        class Ruler;
        class Lanes;
        class TrackHeader;

        void timerCallback() override;
        void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
        void rebuild();                    // tracks added/removed/reordered
        void refresh();                    // values changed
        int barsShown() const;             // the song plus some room
        int totalBars() const;             // scrollable extent

        // Visible range of the timeline, shared by the ruler and the lanes.
        double visibleTicks() const;
        void setView (double startTick, double bars);
        void zoomAround (double factor, float xInThis);
        float zoomAnchorX() const;
        // Zoom (Ctrl, or `plainWheelZooms`) or scroll (Shift / horizontal wheel); false if not handled.
        bool handleWheel (const juce::MouseEvent& inThis, const juce::MouseWheelDetails&, bool plainWheelZooms = false);
        void setTrackDropRow (int row);
        void updateScrollBar();

        audio::AudioEngine& engine;
        project::Session& session;
        Selection& selection;
        int sessionListener = 0, selectionListener = 0;

        std::unique_ptr<Ruler> ruler;
        std::unique_ptr<Lanes> lanes;
        juce::Viewport viewport;
        juce::TextButton addDrumsButton { "+ Drums" }, addSynthButton { "+ Synth" };
        juce::ScrollBar hScroll { false };
        juce::TextButton zoomOutButton { juce::String::fromUTF8 ("\xe2\x88\x92") }, zoomInButton { "+" }, fitButton { "Fit" };
        double viewStart = 0.0, viewBars = 16.0;
        double lastPosition = 0.0;
        std::optional<project::Clip> clipboard;
        project::TrackType clipboardType = project::TrackType::drums;
        std::vector<project::TrackId> shownTracks;
    };
}
