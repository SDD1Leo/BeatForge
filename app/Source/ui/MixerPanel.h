#pragma once

#include "ui/EditorPage.h"
#include "ui/LevelMeterComponent.h"
#include "ui/Selection.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace beatforge
{
    // Mixer: one channel strip per track (colour, name, pan, fader with
    // a post-fader meter, dB readout, mute/solo) and the master strip (master
    // volume, output meter). Everything goes through the Session; a fader drag
    // is one undo step. Clicking a strip selects its track.
    class MixerPanel final : public EditorPage
    {
    public:
        MixerPanel (audio::AudioEngine&, project::Session&, Selection&);
        ~MixerPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int stripWidth = 96;

    private:
        class Strip;
        void rebuild();
        void refresh();

        audio::AudioEngine& engine;
        project::Session& session;
        Selection& selection;
        int sessionListener = 0, selectionListener = 0;

        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<Strip>> strips;
        std::unique_ptr<Strip> master;
        std::vector<project::TrackId> shownTracks;
    };
}
