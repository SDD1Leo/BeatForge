#include "ui/MixerPanel.h"
#include "ui/Theme.h"

#include <cmath>

namespace beatforge
{
    //==============================================================================
    class MixerPanel::Strip final : public juce::Component,
                                    public juce::SettableTooltipClient
    {
    public:
        // trackId 0 = the master strip.
        Strip (MixerPanel& o, project::TrackId t)
            : owner (o), trackId (t),
              meter (t == 0 ? o.engine.getOutputMeter() : o.engine.getSlotMeter (o.session.slotOf (t)))
        {
            meter.setShowScale (false);
            meter.setTooltip ("Level after the fader (click to reset the clip light)");
            addAndMakeVisible (meter);

            fader.setSliderStyle (juce::Slider::LinearVertical);
            fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            fader.setRange (project::Project::minVolumeDb, project::Project::maxVolumeDb, 0.1);
            fader.setSkewFactorFromMidPoint (-12.0);
            fader.setDoubleClickReturnValue (true, 0.0);
            fader.setWantsKeyboardFocus (false);
            fader.onDragStart = [this] { owner.session.beginGesture(); };
            fader.onDragEnd = [this] { owner.session.endGesture(); };
            fader.onValueChange = [this]
            {
                if (trackId == 0) owner.session.setMasterVolume ((float) fader.getValue());
                else              owner.session.setTrackVolume (trackId, (float) fader.getValue());
                repaint();
            };
            addAndMakeVisible (fader);

            if (trackId != 0)
            {
                pan.setSliderStyle (juce::Slider::RotaryVerticalDrag);
                pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
                pan.setRange (-1.0, 1.0, 0.01);
                pan.setDoubleClickReturnValue (true, 0.0);
                pan.setPopupDisplayEnabled (true, true, nullptr);
                pan.textFromValueFunction = [] (double v)
                {
                    const auto pct = (int) std::round (std::abs (v) * 100.0);
                    return pct == 0 ? juce::String ("C") : juce::String (pct) + (v < 0 ? " L" : " R");
                };
                pan.setWantsKeyboardFocus (false);
                pan.onDragStart = [this] { owner.session.beginGesture(); };
                pan.onDragEnd = [this] { owner.session.endGesture(); };
                pan.onValueChange = [this] { owner.session.setTrackPan (trackId, (float) pan.getValue()); };
                addAndMakeVisible (pan);

                for (auto* b : { &mute, &solo })
                {
                    b->setClickingTogglesState (true);
                    b->setWantsKeyboardFocus (false);
                    b->setColour (juce::TextButton::buttonColourId, theme::bgControl);
                    b->setColour (juce::TextButton::textColourOffId, theme::textSecondary);
                    b->setColour (juce::TextButton::textColourOnId, theme::bgApp);
                    addAndMakeVisible (*b);
                }
                mute.setColour (juce::TextButton::buttonOnColourId, theme::muteOn);
                solo.setColour (juce::TextButton::buttonOnColourId, theme::soloOn);
                mute.onClick = [this] { owner.session.setTrackMuted (trackId, mute.getToggleState()); };
                solo.onClick = [this] { owner.session.setTrackSoloed (trackId, solo.getToggleState()); };
            }
            refresh();
        }

        void refresh()
        {
            const auto& p = owner.session.getProject();
            if (trackId == 0)
            {
                name = "MASTER";
                colour = theme::accent;
                fader.setValue (p.masterVolumeDb, juce::dontSendNotification);
                fader.setTooltip ("Master volume (double-click: 0 dB)");
                setTooltip ("Master: the sum of all tracks (the metronome is not affected)");
            }
            else if (const auto* t = owner.session.findTrack (trackId))
            {
                name = juce::String::fromUTF8 (t->name.c_str());
                colour = owner.selection.colourOf (trackId);
                fader.setValue (t->volumeDb, juce::dontSendNotification);
                pan.setValue (t->pan, juce::dontSendNotification);
                mute.setToggleState (t->muted, juce::dontSendNotification);
                solo.setToggleState (t->soloed, juce::dontSendNotification);
                pan.setColour (juce::Slider::rotarySliderFillColourId, colour);
                fader.setTooltip (name + " volume (double-click: 0 dB)");
                pan.setTooltip (name + " pan (double-click: centre)");
                mute.setTooltip ("Mute " + name);
                solo.setTooltip ("Solo " + name);
                setTooltip (name + ": click to select");
            }
            fader.setColour (juce::Slider::trackColourId, colour);
            repaint();
        }

        project::TrackId getTrackId() const noexcept { return trackId; }

        // A card like the track headers: colour strip on top, name, pan, fader
        // with its meter, the dB value, then M / S.
        void paint (juce::Graphics& g) override
        {
            const bool selected = trackId != 0 && owner.selection.getTrack() == trackId;
            const auto card = getLocalBounds().toFloat().reduced (4.0f, 0.0f);
            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path shape;
                shape.addRoundedRectangle (card, theme::cardRadius);
                g.reduceClipRegion (shape);
                g.setColour (selected ? theme::bgRaised : theme::bgSurface);
                g.fillRect (card);
                g.setColour (colour);
                g.fillRect (card.withHeight (4.0f));
            }
            g.setColour (selected ? colour : theme::borderSubtle);
            g.drawRoundedRectangle (card.reduced (0.5f), theme::cardRadius, 1.0f);

            const auto* t = trackId != 0 ? owner.session.findTrack (trackId) : nullptr;
            g.setColour (trackId == 0 ? theme::textMuted : (t != nullptr && t->muted ? theme::textMuted : theme::textPrimary));
            g.setFont (trackId == 0 ? theme::captionFont (10.0f) : theme::font (12.0f, juce::Font::bold));
            g.drawFittedText (name, nameArea, juce::Justification::centred, 1, 0.8f);

            const auto db = fader.getValue();
            g.setColour (theme::textSecondary);
            g.setFont (theme::numericFont (11.0f));
            g.drawText (db <= project::Project::minVolumeDb ? juce::String ("-inf") : juce::String (db, 1) + " dB",
                        valueArea, juce::Justification::centred);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12, 10);
            area.removeFromTop (4);
            nameArea = area.removeFromTop (20);
            area.removeFromTop (6);
            if (trackId != 0)
            {
                pan.setBounds (area.removeFromTop (34).withSizeKeepingCentre (34, 34));
                area.removeFromTop (6);
                auto buttons = area.removeFromBottom (22).withSizeKeepingCentre (24 * 2 + 6, 22);
                mute.setBounds (buttons.removeFromLeft (24));
                solo.setBounds (buttons.removeFromRight (24));
                area.removeFromBottom (6);
            }
            else
            {
                area.removeFromTop (40);
                area.removeFromBottom (28);
            }
            valueArea = area.removeFromBottom (16);
            area.removeFromBottom (4);
            auto faderArea = area.withSizeKeepingCentre (56, area.getHeight());
            meter.setBounds (faderArea.removeFromRight (18));
            fader.setBounds (faderArea);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (trackId != 0)
                owner.selection.selectTrack (trackId);
        }

    private:
        MixerPanel& owner;
        project::TrackId trackId;
        LevelMeterComponent meter;
        juce::Slider fader, pan;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::String name;
        juce::Colour colour;
        juce::Rectangle<int> nameArea, valueArea;
    };

    //==============================================================================
    MixerPanel::MixerPanel (audio::AudioEngine& e, project::Session& s, Selection& sel)
        : engine (e), session (s), selection (sel)
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);
        viewport.setScrollBarThickness (8);
        addAndMakeVisible (viewport);

        master = std::make_unique<Strip> (*this, 0);
        addAndMakeVisible (*master);

        sessionListener = session.addListener ([this] (project::Session::Change change)
        {
            if (change != project::Session::Change::metadata && change != project::Session::Change::instrument)
                rebuild();
        });
        selectionListener = selection.addListener ([this] { refresh(); });
        rebuild();
    }

    MixerPanel::~MixerPanel()
    {
        session.removeListener (sessionListener);
        selection.removeListener (selectionListener);
    }

    void MixerPanel::rebuild()
    {
        std::vector<project::TrackId> ids;
        for (auto& t : session.getProject().tracks)
            ids.push_back (t.id);

        if (ids != shownTracks)
        {
            shownTracks = ids;
            strips.clear();
            for (auto id : ids)
            {
                strips.push_back (std::make_unique<Strip> (*this, id));
                content.addAndMakeVisible (*strips.back());
            }
            resized();
        }
        refresh();
    }

    void MixerPanel::refresh()
    {
        for (auto& s : strips)
            s->refresh();
        master->refresh();
    }

    void MixerPanel::paint (juce::Graphics& g)
    {
        if (const auto* t = session.findTrack (selection.getTrack()))
            paintContext (g, selection.colourOf (t->id), juce::String::fromUTF8 (t->name.c_str()),
                          t->type == project::TrackType::drums ? "DRUMS" : "SYNTH");

        g.setColour (theme::textMuted);
        g.setFont (theme::font (11.0f));
        g.drawText (juce::String ((int) strips.size()) + (strips.size() == 1 ? " track" : " tracks")
                        + juce::String::fromUTF8 ("  \xc2\xb7  faders: drag, double-click = 0 dB  \xc2\xb7  click a strip to select its track"),
                    toolRow(), juce::Justification::centredRight, false);
    }

    void MixerPanel::resized()
    {
        auto area = getLocalBounds().withTrimmedTop (headerHeight).reduced (12, 12);

        master->setBounds (area.removeFromRight (stripWidth));
        area.removeFromRight (12);
        viewport.setBounds (area);

        const auto height = area.getHeight() - (stripWidth * (int) strips.size() > area.getWidth() ? viewport.getScrollBarThickness() : 0);
        content.setSize (juce::jmax (area.getWidth(), stripWidth * (int) strips.size()), height);
        for (size_t i = 0; i < strips.size(); ++i)
            strips[i]->setBounds ((int) i * stripWidth, 0, stripWidth, height);
    }
}
