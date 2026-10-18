#include "ui/DrumMachinePanel.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr int numLanes = audio::DrumMachine::numLanes;
        constexpr int numSteps = audio::DrumMachine::numSteps;
        constexpr int rowHeight = 44;
        constexpr int headerWidth = 310;

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
                b->setColour (juce::TextButton::textColourOnId, theme::background);
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

        void paint (juce::Graphics& g) override
        {
            const auto colour = laneColour (lane);
            auto area = nameArea.toFloat().reduced (0.0f, 6.0f);

            // Track-header style: dark plate, solid track-colour stripe, a hit
            // light that flashes on every trigger.
            g.setColour (theme::control.interpolatedWith (colour, 0.18f * flash));
            g.fillRoundedRectangle (area, theme::controlRadius);
            g.setColour (colour);
            g.fillRoundedRectangle (area.withWidth (4.0f), 2.0f);

            if (editor != nullptr)
                return;

            area.removeFromLeft (10.0f);
            auto led = area.removeFromLeft (14.0f).withSizeKeepingCentre (7.0f, 7.0f);
            g.setColour (flash > 0.0f ? colour.withAlpha (0.4f + 0.6f * flash) : theme::border);
            g.fillEllipse (led);
            area.removeFromLeft (4.0f);

            g.setColour (theme::textPrimary);
            g.setFont (theme::font (13.5f, juce::Font::bold));
            g.drawText (name, area.withTrimmedRight (4.0f), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            nameArea = area.removeFromLeft (120);
            area.removeFromLeft (8);
            mute.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 26));
            area.removeFromLeft (4);
            solo.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 26));
            area.removeFromLeft (8);
            volume.setBounds (area.reduced (0, 8));

            if (editor != nullptr)
                editor->setBounds (nameArea.reduced (8, 9).withTrimmedLeft (6));
        }

    private:
        void showEditor()
        {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont (theme::font (13.5f, juce::Font::bold));
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

        void paint (juce::Graphics& g) override
        {
            const auto* clip = owner.clip();
            if (clip == nullptr)
                return;
            const auto& pattern = clip->pattern;

            for (int step = 0; step < numSteps; ++step)
            {
                // Empty cells in every other beat group are lighter so the 4x4
                // structure is readable.
                const auto offColour = (step / 4) % 2 == 1 ? theme::control.brighter (0.08f) : theme::control;

                for (int lane = 0; lane < numLanes; ++lane)
                {
                    const auto cell = cellBounds (lane, step).toFloat().reduced (3.0f, 6.0f);
                    const bool on = pattern[(size_t) lane][(size_t) step];
                    const auto colour = laneColour (lane);

                    g.setColour (on ? colour : offColour);
                    g.fillRoundedRectangle (cell, theme::controlRadius);

                    // Current step: lifts every cell in the column; a lit step flares.
                    if (step == playingStep)
                    {
                        g.setColour (on ? juce::Colours::white.withAlpha (0.45f) : theme::textPrimary.withAlpha (0.16f));
                        g.fillRoundedRectangle (cell, theme::controlRadius);
                    }

                    g.setColour (on ? colour.brighter (0.3f) : (step == playingStep ? theme::borderStrong : theme::border));
                    g.drawRoundedRectangle (cell, theme::controlRadius, theme::borderWidth);
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

        float cellWidth() const { return (float) getWidth() / numSteps; }

        juce::Rectangle<int> columnBounds (int step) const
        {
            const auto x0 = juce::roundToInt (step * cellWidth()), x1 = juce::roundToInt ((step + 1) * cellWidth());
            return { x0, 0, x1 - x0, getHeight() };
        }

        juce::Rectangle<int> cellBounds (int lane, int step) const
        {
            return columnBounds (step).withY (lane * rowHeight).withHeight (rowHeight);
        }

        std::pair<int, int> cellAt (juce::Point<int> p) const
        {
            if (! getLocalBounds().contains (p))
                return { -1, -1 };
            return { juce::jlimit (0, numLanes - 1, p.y / rowHeight),
                     juce::jlimit (0, numSteps - 1, (int) ((float) p.x / cellWidth())) };
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
        grid->setPlayingStep (step);

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
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius, 1.0f);

        // Title: caption, track name and where the clip sits.
        auto title = titleArea.withWidth (headerWidth - 80);
        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont());
        g.drawText ("STEP SEQUENCER", title.removeFromLeft (118), juce::Justification::centredLeft);

        const auto* t = track();
        const auto* c = clip();
        if (t != nullptr)
        {
            g.setColour (selection.colourOf (t->id));
            g.setFont (theme::font (13.0f, juce::Font::bold));
            g.drawText (juce::String::fromUTF8 (t->name.c_str()), title, juce::Justification::centredLeft, true);
        }

        if (c == nullptr)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            const auto message = t == nullptr ? "Select a drum track or one of its clips in the arrangement."
                                              : "This drum track has no clip selected. Double-click its lane in the arrangement to create one.";
            g.drawFittedText (message, getLocalBounds().withTrimmedLeft (t != nullptr ? headerWidth : 0).reduced (24),
                              juce::Justification::centred, 2);
            return;
        }

        const auto perBar = session.getProject().timeSignature.ticksPerBar();
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (12.0f));
        const auto bars = juce::String ((double) c->length / (double) perBar, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        g.drawText ("Clip at bar " + juce::String (c->start / perBar + 1) + ", " + bars + (c->length == perBar ? " bar" : " bars")
                        + (c->length > perBar ? ", pattern repeats every bar" : ""),
                    titleArea.withTrimmedLeft (headerWidth + 4).withRight (getWidth() - 14), juce::Justification::centredRight);

        // Step numbers above the grid.
        const auto gridBounds = grid->getBounds();
        g.setFont (theme::font (10.0f));
        for (int step = 0; step < numSteps; ++step)
        {
            const float w = (float) gridBounds.getWidth() / numSteps;
            g.setColour (step % 4 == 0 ? theme::textPrimary : theme::textSecondary);
            g.drawText (juce::String (step + 1),
                        juce::Rectangle<float> (gridBounds.getX() + step * w, (float) stepNumbers.getY(), w, (float) stepNumbers.getHeight()),
                        juce::Justification::centred);
        }
    }

    void DrumMachinePanel::resized()
    {
        auto area = getLocalBounds().reduced (12, 6);
        titleArea = area.removeFromTop (32);
        stepNumbers = area.removeFromTop (18);
        clearButton.setBounds (titleArea.withLeft (titleArea.getX() + headerWidth - 70).withWidth (60).reduced (0, 4));

        auto headerColumn = area.removeFromLeft (headerWidth);
        for (auto& h : headers)
            h->setBounds (headerColumn.removeFromTop (rowHeight).withTrimmedRight (12));

        grid->setBounds (area.withHeight (rowHeight * numLanes));
    }
}
