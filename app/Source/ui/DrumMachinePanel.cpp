#include "ui/DrumMachinePanel.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr int numLanes = audio::DrumMachine::numLanes;
        constexpr int numSteps = audio::DrumMachine::numSteps;
        constexpr int rowHeight = 52, cellHeight = 44;   // spec 8: rows 52, cells 44
        constexpr int headerWidth = 316;                  // name chip, M, S, level
        constexpr int glowMargin = 8;                     // room around the grid for the playhead glow
        constexpr int groupGap = 16, cellGap = 6;

    }

    juce::Colour DrumMachinePanel::laneColour (int lane)
    {
        static const juce::Colour colours[] { theme::track::kick, theme::track::snare, theme::track::closedHat,
                                              theme::track::openHat, theme::track::clap };
        return colours[(size_t) juce::jlimit (0, numLanes - 1, lane)];
    }

    //==============================================================================
    // Name (click to audition, flashes on every hit), mute, solo, volume.
    class DrumMachinePanel::LaneHeader final : public juce::Component,
                                               public juce::SettableTooltipClient
    {
    public:
        LaneHeader (DrumMachinePanel& o, int laneIndex) : owner (o), session (o.session), lane (laneIndex)
        {
            const auto colour = laneColour (lane);

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
            mute.onClick = [this] { session.setLaneMuted (owner.trackId(), lane, mute.getToggleState()); };
            solo.onClick = [this] { session.setLaneSoloed (owner.trackId(), lane, solo.getToggleState()); };

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (audio::DrumMachine::minGainDb, audio::DrumMachine::maxGainDb, 0.1);
            volume.setSkewFactorFromMidPoint (-12.0);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setPopupDisplayEnabled (true, true, nullptr);
            volume.setTextValueSuffix (" dB");
            volume.setNumDecimalPlacesToDisplay (1);
            volume.setWantsKeyboardFocus (false);
            volume.setColour (juce::Slider::trackColourId, colour);
            volume.onDragStart = [this] { session.beginGesture(); };
            volume.onDragEnd = [this] { session.endGesture(); };
            volume.onValueChange = [this] { session.setLaneGainDb (owner.trackId(), lane, (float) volume.getValue()); };
            addAndMakeVisible (volume);

            refresh();
        }

        std::function<void()> onEditFinished;

        void refresh()
        {
            const auto* t = owner.track();
            if (t == nullptr)
                return;
            const auto& l = t->lanes[(size_t) lane];
            name = juce::String::fromUTF8 (l.name.c_str());
            mute.setToggleState (l.muted, juce::dontSendNotification);
            solo.setToggleState (l.soloed, juce::dontSendNotification);
            volume.setValue (l.gainDb, juce::dontSendNotification);
            mute.setTooltip ("Mute " + name);
            solo.setTooltip ("Solo " + name);
            volume.setTooltip (name + " volume (double-click: 0 dB)");
            setTooltip ("Click to hear " + name + ", double-click to rename");
            repaint();
        }

        void setFlash (float amount)
        {
            if (! juce::exactlyEqual (amount, flash))
            {
                flash = amount;
                repaint (nameArea);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (nameArea.contains (e.getPosition()) && e.getNumberOfClicks() == 1)
                if (owner.slot() >= 0) owner.engine.drums (owner.slot()).preview (lane);
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (nameArea.contains (e.getPosition()))
                showEditor();
        }

        // Name chip (spec 8): bg/surface card with a colour bar, a hit light that
        // flashes on every trigger, and the name.
        void paint (juce::Graphics& g) override
        {
            const auto colour = laneColour (lane);
            const auto chip = nameArea.toFloat();

            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path shape;
                shape.addRoundedRectangle (chip, 8.0f);
                g.reduceClipRegion (shape);
                g.setColour (theme::bgSurface.interpolatedWith (colour, 0.22f * flash));
                g.fillRect (chip);
                g.setColour (colour);
                g.fillRect (chip.withWidth (4.0f));
            }
            g.setColour (theme::borderSubtle);
            g.drawRoundedRectangle (chip.reduced (0.5f), 8.0f, 1.0f);

            if (editor != nullptr)
                return;

            auto area = chip.withTrimmedLeft (14.0f);
            g.setColour (flash > 0.0f ? colour.withAlpha (0.4f + 0.6f * flash) : theme::bgControl);
            g.fillEllipse (area.removeFromLeft (6.0f).withSizeKeepingCentre (6.0f, 6.0f));
            area.removeFromLeft (8.0f);

            const auto* t = owner.track();
            const bool muted = t != nullptr && t->lanes[(size_t) lane].muted;
            g.setColour (muted ? theme::textMuted : theme::textPrimary);
            g.setFont (theme::font (12.0f, juce::Font::bold));
            g.drawText (name, area.withTrimmedRight (6.0f), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto area = getLocalBounds().withSizeKeepingCentre (getWidth(), cellHeight);
            nameArea = area.removeFromLeft (132);
            area.removeFromLeft (10);
            mute.setBounds (area.removeFromLeft (24).withSizeKeepingCentre (24, 22));
            area.removeFromLeft (6);
            solo.setBounds (area.removeFromLeft (24).withSizeKeepingCentre (24, 22));
            area.removeFromLeft (12);
            volume.setBounds (area.withTrimmedRight (16).withSizeKeepingCentre (area.getWidth() - 16, 14).withX (area.getX()));

            if (editor != nullptr)
                editor->setBounds (nameArea.reduced (8, 10).withTrimmedLeft (20));
        }

    private:
        void showEditor()
        {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont (theme::font (12.0f, juce::Font::bold));
            editor->setInputRestrictions (project::Project::maxLaneNameLength);
            editor->setText (name, false);
            editor->selectAll();
            editor->onReturnKey = [this] { closeEditor (true); };
            editor->onFocusLost = [this] { closeEditor (true); };
            editor->onEscapeKey = [this] { closeEditor (false); };
            addAndMakeVisible (*editor);
            resized();
            editor->grabKeyboardFocus();
            repaint();
        }

        void closeEditor (bool commit)
        {
            if (editor == nullptr || ! editor->isVisible())
                return;

            const auto text = editor->getText().trim();
            editor->setVisible (false);

            if (commit && text.isNotEmpty())
                session.renameLane (owner.trackId(), lane, text.toStdString());   // invalid names are refused

            // The editor is still inside its own callback; delete it afterwards.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LaneHeader> (this)]
            {
                if (safe == nullptr)
                    return;
                safe->editor = nullptr;
                safe->refresh();
                if (safe->onEditFinished)
                    safe->onEditFinished();
            });
        }

        DrumMachinePanel& owner;
        project::Session& session;
        int lane;
        juce::String name;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::Slider volume;
        juce::Rectangle<int> nameArea;
        std::unique_ptr<juce::TextEditor> editor;
        float flash = 0.0f;
    };

    //==============================================================================
    class DrumMachinePanel::StepGrid final : public juce::Component,
                                             public juce::SettableTooltipClient
    {
    public:
        explicit StepGrid (DrumMachinePanel& o) : owner (o), engine (o.engine), session (o.session)
        {
            setTooltip ("Click or drag to set steps");
        }

        void setPlayingStep (int step)
        {
            if (step != playingStep)
            {
                playingStep = step;
                repaint();
            }
        }

        int getPlayingStep() const noexcept { return playingStep; }

        // Spec 12.4: off cells alternate bg/raised and bg/control by beat group;
        // on cells take the row colour with a shine; the playing column is
        // outlined, and its lit cells glow.
        void paint (juce::Graphics& g) override
        {
            const auto* clip = owner.clip();
            if (clip == nullptr)
                return;
            const auto& pattern = clip->pattern;

            if (playingStep >= 0)
                for (int lane = 0; lane < numLanes; ++lane)
                    if (pattern[(size_t) lane][(size_t) playingStep])
                    {
                        juce::Path cell;
                        cell.addRoundedRectangle (cellBounds (lane, playingStep).toFloat(), 5.0f);
                        juce::DropShadow (laneColour (lane).withAlpha (0.8f), 14, {}).drawForPath (g, cell);
                    }

            for (int step = 0; step < numSteps; ++step)
            {
                const bool accentGroup = (step / 4) % 2 == 1;
                const bool playing = step == playingStep;

                for (int lane = 0; lane < numLanes; ++lane)
                {
                    const auto cell = cellBounds (lane, step).toFloat();
                    const bool on = pattern[(size_t) lane][(size_t) step];

                    if (on)
                    {
                        const auto colour = laneColour (lane);
                        juce::Graphics::ScopedSaveState state (g);
                        juce::Path shape;
                        shape.addRoundedRectangle (cell, 5.0f);
                        g.reduceClipRegion (shape);
                        g.setColour (colour);
                        g.fillRect (cell);
                        g.setColour (theme::shadow.withAlpha (0.25f));
                        g.fillRect (cell.withTop (cell.getBottom() - 2.0f));
                        g.setColour (juce::Colour (theme::textPrimary).withAlpha (0.35f));
                        g.fillRoundedRectangle (cell.reduced (4.0f, 0.0f).withY (cell.getY() + 4.0f).withHeight (2.0f), 1.0f);
                        continue;
                    }

                    g.setColour (playing || accentGroup ? theme::bgControl : theme::bgRaised);
                    g.fillRoundedRectangle (cell, 5.0f);
                    g.setColour (playing ? theme::textPrimary.withAlpha (0.6f) : (accentGroup ? theme::border : theme::borderSubtle));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 5.0f, 1.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto [lane, step] = cellAt (e.getPosition());
            if (lane < 0)
                return;

            // The first cell decides whether this drag paints steps on or off.
            if (owner.clip() == nullptr)
                return;
            session.beginGesture();
            painting = true;
            paintValue = ! owner.clip()->pattern[(size_t) lane][(size_t) step];
            paintLane = lane;
            apply (lane, step);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto [lane, step] = cellAt (e.getPosition());
            if (painting && lane == paintLane && step >= 0)
                apply (lane, step);
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (painting)
                session.endGesture();
            painting = false;
        }

    private:
        void apply (int lane, int step)
        {
            const auto* clip = owner.clip();
            if (clip == nullptr || clip->pattern[(size_t) lane][(size_t) step] == paintValue)
                return;

            session.setStep (clip->id, lane, step, paintValue);

            // Audition is not an edit: it goes straight to the engine.
            if (paintValue && engine.getState() == audio::TransportState::stopped && owner.slot() >= 0)
                engine.drums (owner.slot()).preview (lane);

            repaint (cellBounds (lane, step));
        }

    public:
        // Inner grid area (the component keeps a margin for the glow).
        juce::Rectangle<int> inner() const { return getLocalBounds().reduced (glowMargin); }

        float cellWidth() const
        {
            return ((float) inner().getWidth() - 3.0f * groupGap - 12.0f * cellGap) / (float) numSteps;
        }

        float columnX (int step) const
        {
            return (float) inner().getX() + (float) step * (cellWidth() + cellGap) + (float) (step / 4) * (groupGap - cellGap);
        }

        juce::Rectangle<int> cellBounds (int lane, int step) const
        {
            const auto x0 = juce::roundToInt (columnX (step)), x1 = juce::roundToInt (columnX (step) + cellWidth());
            const auto y = inner().getY() + lane * rowHeight + (rowHeight - cellHeight) / 2;
            return { x0, y, x1 - x0, cellHeight };
        }

    private:
        // The nearest column within the gaps, so a drag across a gap keeps painting.
        std::pair<int, int> cellAt (juce::Point<int> p) const
        {
            const auto area = inner();
            if (! area.expanded (glowMargin / 2).contains (p))
                return { -1, -1 };
            int best = 0;
            float bestDistance = std::numeric_limits<float>::max();
            for (int step = 0; step < numSteps; ++step)
            {
                const auto d = std::abs ((float) p.x - (columnX (step) + cellWidth() * 0.5f));
                if (d < bestDistance) { bestDistance = d; best = step; }
            }
            return { juce::jlimit (0, numLanes - 1, (p.y - area.getY()) / rowHeight), best };
        }

        DrumMachinePanel& owner;
        audio::AudioEngine& engine;
        project::Session& session;
        int playingStep = -1, paintLane = -1;
        bool paintValue = true, painting = false;
    };

    //==============================================================================
    DrumMachinePanel::DrumMachinePanel (audio::AudioEngine& e, project::Session& s, Selection& sel)
        : engine (e), session (s), selection (sel)
    {
        grid = std::make_unique<StepGrid> (*this);
        addAndMakeVisible (*grid);

        for (int lane = 0; lane < numLanes; ++lane)
        {
            headers[(size_t) lane] = std::make_unique<LaneHeader> (*this, lane);
            headers[(size_t) lane]->onEditFinished = [this] { if (onTextEditFinished) onTextEditFinished(); };
            addAndMakeVisible (*headers[(size_t) lane]);
        }

        clearButton.setTooltip ("Clear all steps of this pattern");
        clearButton.setWantsKeyboardFocus (false);
        clearButton.onClick = [this] { session.clearPattern (clipId()); };
        addAndMakeVisible (clearButton);

        listenerId = session.addListener ([this] (project::Session::Change change)
        {
            if (change != project::Session::Change::metadata && change != project::Session::Change::instrument)
                refreshAll();
        });
        selectionListener = selection.addListener ([this] { refreshAll(); });

        refreshAll();
        startTimerHz (60);
    }

    DrumMachinePanel::~DrumMachinePanel()
    {
        session.removeListener (listenerId);
        selection.removeListener (selectionListener);
    }

    project::TrackId DrumMachinePanel::trackId() const
    {
        const auto* t = session.findTrack (selection.getTrack());
        return t != nullptr && t->type == project::TrackType::drums ? t->id : 0;
    }

    project::ClipId DrumMachinePanel::clipId() const
    {
        return trackId() != 0 ? selection.getClip() : 0;
    }

    const project::Track* DrumMachinePanel::track() const { return session.findTrack (trackId()); }
    const project::Clip* DrumMachinePanel::clip() const   { return session.findClip (clipId()); }
    int DrumMachinePanel::slot() const                    { return session.slotOf (trackId()); }

    void DrumMachinePanel::refreshAll()
    {
        const bool hasTrack = trackId() != 0, hasClip = clip() != nullptr;
        for (auto& h : headers)
        {
            h->setVisible (hasTrack);
            h->refresh();
        }
        grid->setVisible (hasClip);
        clearButton.setVisible (hasClip);
        grid->repaint();
        repaint();
    }

    void DrumMachinePanel::timerCallback()
    {
        if (! isShowing())
            return;

        // Playing step, from the musical position (display only).
        // Playing step: only while the playhead is inside the edited clip.
        int step = -1;
        if (const auto* c = clip(); c != nullptr && engine.getState() == audio::TransportState::playing)
        {
            const auto pos = engine.getPositionTicks();
            if (pos >= (double) c->start && pos < (double) c->end())
                step = (int) ((pos - (double) c->start) / (double) time::Transport::stepTicks) % numSteps;
        }
        if (step != grid->getPlayingStep())
        {
            grid->setPlayingStep (step);
            repaint (stepNumbers);
        }

        // Trigger lights: flash when a lane's hit count changes, then fade.
        for (int lane = 0; lane < numLanes; ++lane)
        {
            const auto count = slot() >= 0 ? engine.drums (slot()).getTriggerCount (lane) : lastTriggerCounts[(size_t) lane];
            auto& flash = flashes[(size_t) lane];

            if (count != lastTriggerCounts[(size_t) lane])
            {
                lastTriggerCounts[(size_t) lane] = count;
                flash = 1.0f;
            }
            else
            {
                flash = juce::jmax (0.0f, flash - 0.12f);
            }

            headers[(size_t) lane]->setFlash (flash);
        }
    }

    void DrumMachinePanel::paint (juce::Graphics& g)
    {
        const auto* t = track();
        const auto* c = clip();
        if (const auto* selected = session.findTrack (selection.getTrack()))
            paintContext (g, selection.colourOf (selected->id), juce::String::fromUTF8 (selected->name.c_str()),
                          selected->type == project::TrackType::drums ? "DRUMS" : "SYNTH");

        if (t == nullptr)
            return paintEmpty (g, "Select a drum track or one of its clips in the arrangement.");

        if (c == nullptr)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawFittedText ("This drum track has no clip selected. Double-click its lane in the arrangement to create one.",
                              getLocalBounds().withTrimmedTop (headerHeight).withTrimmedLeft (16 + headerWidth).reduced (24),
                              juce::Justification::centred, 2);
            return;
        }

        // Header: the clip chip, a divider, then Clear.
        {
            const auto perBar = session.getProject().timeSignature.ticksPerBar();
            const auto bars = juce::String ((double) c->length / (double) perBar, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            const auto where = juce::String ("Clip at bar ") + juce::String (c->start / perBar + 1) + juce::String::fromUTF8 (" \xc2\xb7 ")
                             + bars + (c->length == perBar ? " bar" : " bars")
                             + (c->length > perBar ? juce::String::fromUTF8 (" \xc2\xb7 repeats every bar") : juce::String());
            const auto steps = juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (numSteps) + " steps";
            const juce::Font text (theme::font (11.0f)), mono (theme::numericFont (11.0f));
            const auto w1 = juce::GlyphArrangement::getStringWidthInt (text, where), w2 = juce::GlyphArrangement::getStringWidthInt (mono, steps);
            auto chip = clipChip.withLeft (clipChip.getRight() - (w1 + w2 + 22));
            g.setColour (theme::bgSurface);
            g.fillRoundedRectangle (chip.toFloat(), 6.0f);
            g.setColour (theme::borderSubtle);
            g.drawRoundedRectangle (chip.toFloat().reduced (0.5f), 6.0f, 1.0f);
            auto inner = chip.reduced (11, 0);
            g.setColour (theme::textSecondary);
            g.setFont (text);
            g.drawText (where, inner.removeFromLeft (w1), juce::Justification::centredLeft, false);
            g.setFont (mono);
            g.drawText (steps, inner, juce::Justification::centredLeft, false);
            g.setColour (theme::border);
            g.fillRect (headerDivider);
        }

        // Step numbers: the first of each group brighter, the playing step purple.
        g.setColour (theme::textMuted);
        g.setFont (theme::captionFont (9.0f));
        g.drawText ("SOUND", stepNumbers.withWidth (headerWidth), juce::Justification::centredLeft, false);
        g.setFont (theme::numericFont (10.0f, juce::Font::bold));
        const auto playing = grid->getPlayingStep();
        for (int step = 0; step < numSteps; ++step)
        {
            const auto cell = grid->cellBounds (0, step) + grid->getPosition();
            g.setColour (step == playing ? theme::purple : (step % 4 == 0 ? theme::textSecondary : theme::textMuted));
            g.drawText (juce::String (step + 1), cell.withY (stepNumbers.getY()).withHeight (stepNumbers.getHeight()),
                        juce::Justification::centred, false);
        }
    }

    void DrumMachinePanel::resized()
    {
        // Header tools (right): clip chip, divider, Clear.
        auto tools = toolRow();
        clearButton.setBounds (tools.removeFromRight (64));
        tools.removeFromRight (12);
        headerDivider = tools.removeFromRight (1).withSizeKeepingCentre (1, 18);
        tools.removeFromRight (12);
        clipChip = tools.withSizeKeepingCentre (tools.getWidth(), 28);

        // Body: padding 16 / 14, the step numbers, then one row per sound.
        auto area = getLocalBounds().withTrimmedTop (headerHeight).reduced (16, 14);
        stepNumbers = area.removeFromTop (18);
        area.removeFromTop (6);

        auto headerColumn = area.removeFromLeft (headerWidth);
        for (auto& h : headers)
            h->setBounds (headerColumn.removeFromTop (rowHeight));

        grid->setBounds (area.withHeight (rowHeight * numLanes).expanded (glowMargin));
    }
}
