#include "ui/DrumMachinePanel.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr int numLanes = audio::DrumMachine::numLanes;
        constexpr int numSteps = audio::DrumMachine::numSteps;
        constexpr int rowHeight = 44;
        constexpr int headerWidth = 300;

        juce::String toJuce (std::string_view s) { return juce::String (s.data(), s.size()); }
    }

    juce::Colour DrumMachinePanel::laneColour (int lane)
    {
        static const juce::Colour colours[] { juce::Colour (0xffff7a2f), juce::Colour (0xffff5c7a), juce::Colour (0xffffd24a),
                                              juce::Colour (0xff4ad7d1), juce::Colour (0xffa98bff) };
        return colours[(size_t) juce::jlimit (0, numLanes - 1, lane)];
    }

    //==============================================================================
    // Name (click to audition, flashes on every hit), mute, solo, volume.
    class DrumMachinePanel::LaneHeader final : public juce::Component,
                                               public juce::SettableTooltipClient
    {
    public:
        LaneHeader (audio::DrumMachine& d, int laneIndex) : drums (d), lane (laneIndex)
        {
            const auto colour = laneColour (lane);

            for (auto* b : { &mute, &solo })
            {
                b->setClickingTogglesState (true);
                b->setWantsKeyboardFocus (false);
                b->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
                b->setColour (juce::TextButton::textColourOnId, theme::background);
                addAndMakeVisible (*b);
            }

            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffb347));
            solo.setColour (juce::TextButton::buttonOnColourId, theme::ok);
            mute.setTooltip ("Mute " + name());
            solo.setTooltip ("Solo " + name());
            mute.onClick = [this] { drums.setLaneMuted (lane, mute.getToggleState()); };
            solo.onClick = [this] { drums.setLaneSoloed (lane, solo.getToggleState()); };

            volume.setSliderStyle (juce::Slider::LinearHorizontal);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (audio::DrumMachine::minGainDb, audio::DrumMachine::maxGainDb, 0.1);
            volume.setSkewFactorFromMidPoint (-12.0);
            volume.setValue (drums.getLaneGainDb (lane), juce::dontSendNotification);
            volume.setDoubleClickReturnValue (true, 0.0);
            volume.setPopupDisplayEnabled (true, true, nullptr);
            volume.setTextValueSuffix (" dB");
            volume.setNumDecimalPlacesToDisplay (1);
            volume.setWantsKeyboardFocus (false);
            volume.setTooltip (name() + " volume (double-click: 0 dB)");
            volume.setColour (juce::Slider::trackColourId, colour.withAlpha (0.7f));
            volume.setColour (juce::Slider::thumbColourId, colour);
            volume.onValueChange = [this] { drums.setLaneGainDb (lane, (float) volume.getValue()); };
            addAndMakeVisible (volume);

            setTooltip ("Click the name to hear " + name());
        }

        juce::String name() const { return toJuce (audio::drumSoundName ((audio::DrumSound) lane)); }

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
            if (nameArea.contains (e.getPosition()))
                drums.preview (lane);
        }

        void paint (juce::Graphics& g) override
        {
            const auto colour = laneColour (lane);
            auto area = nameArea.toFloat().reduced (0.0f, 6.0f);

            g.setColour (theme::panelRaised.interpolatedWith (colour, 0.25f * flash));
            g.fillRoundedRectangle (area, theme::cornerRadius);

            auto led = area.removeFromLeft (22.0f).withSizeKeepingCentre (8.0f, 8.0f);
            g.setColour (colour.withAlpha (0.35f + 0.65f * flash));
            g.fillEllipse (led);

            g.setColour (theme::textPrimary);
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (name(), area, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            nameArea = area.removeFromLeft (110);
            area.removeFromLeft (8);
            mute.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 26));
            area.removeFromLeft (4);
            solo.setBounds (area.removeFromLeft (28).withSizeKeepingCentre (28, 26));
            area.removeFromLeft (8);
            volume.setBounds (area.reduced (0, 8));
        }

    private:
        audio::DrumMachine& drums;
        int lane;
        juce::TextButton mute { "M" }, solo { "S" };
        juce::Slider volume;
        juce::Rectangle<int> nameArea;
        float flash = 0.0f;
    };

    //==============================================================================
    class DrumMachinePanel::StepGrid final : public juce::Component,
                                             public juce::SettableTooltipClient
    {
    public:
        explicit StepGrid (audio::AudioEngine& e) : engine (e)
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
            auto& drums = engine.getDrumMachine();

            for (int step = 0; step < numSteps; ++step)
            {
                // Empty cells in every other beat group are lighter so the 4x4
                // structure is readable.
                const auto offColour = (step / 4) % 2 == 1 ? theme::panelRaised.brighter (0.07f) : theme::panelRaised;

                for (int lane = 0; lane < numLanes; ++lane)
                {
                    const auto cell = cellBounds (lane, step).toFloat().reduced (3.0f, 6.0f);
                    const bool on = drums.getStep (lane, step);
                    const auto colour = laneColour (lane);

                    g.setColour (on ? colour : offColour);
                    g.fillRoundedRectangle (cell, 4.0f);

                    if (step == playingStep)
                    {
                        g.setColour (on ? juce::Colours::white.withAlpha (0.45f) : theme::textPrimary.withAlpha (0.28f));
                        g.fillRoundedRectangle (cell, 4.0f);
                    }

                    g.setColour (on ? colour.brighter (0.2f) : theme::border);
                    g.drawRoundedRectangle (cell, 4.0f, 1.0f);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto [lane, step] = cellAt (e.getPosition());
            if (lane < 0)
                return;

            // The first cell decides whether this drag paints steps on or off.
            paintValue = ! engine.getDrumMachine().getStep (lane, step);
            paintLane = lane;
            apply (lane, step);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            const auto [lane, step] = cellAt (e.getPosition());
            if (lane == paintLane && step >= 0)
                apply (lane, step);
        }

    private:
        void apply (int lane, int step)
        {
            auto& drums = engine.getDrumMachine();
            if (drums.getStep (lane, step) == paintValue)
                return;

            drums.setStep (lane, step, paintValue);

            if (paintValue && engine.getState() == audio::TransportState::stopped)
                drums.preview (lane);

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

        audio::AudioEngine& engine;
        int playingStep = -1, paintLane = -1;
        bool paintValue = true;
    };

    //==============================================================================
    DrumMachinePanel::DrumMachinePanel (audio::AudioEngine& e) : engine (e)
    {
        grid = std::make_unique<StepGrid> (engine);
        addAndMakeVisible (*grid);

        for (int lane = 0; lane < numLanes; ++lane)
        {
            headers[(size_t) lane] = std::make_unique<LaneHeader> (engine.getDrumMachine(), lane);
            addAndMakeVisible (*headers[(size_t) lane]);
        }

        clearButton.setTooltip ("Clear all steps");
        clearButton.setWantsKeyboardFocus (false);
        clearButton.onClick = [this] { engine.getDrumMachine().clearPattern(); grid->repaint(); };
        addAndMakeVisible (clearButton);

        startTimerHz (60);
    }

    DrumMachinePanel::~DrumMachinePanel() = default;

    void DrumMachinePanel::timerCallback()
    {
        // Playing step, from the musical position (display only).
        const bool playing = engine.getState() == audio::TransportState::playing;
        const auto step = (int) (engine.getPositionTicks() / (double) time::Transport::stepTicks) % numSteps;
        grid->setPlayingStep (playing ? step : -1);

        // Trigger lights: flash when a lane's hit count changes, then fade.
        for (int lane = 0; lane < numLanes; ++lane)
        {
            const auto count = engine.getDrumMachine().getTriggerCount (lane);
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

        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("DRUMS", titleArea, juce::Justification::centredLeft);

        // Step numbers above the grid.
        const auto gridBounds = grid->getBounds();
        g.setFont (juce::FontOptions (10.0f));
        for (int step = 0; step < numSteps; ++step)
        {
            const float w = (float) gridBounds.getWidth() / numSteps;
            g.setColour (step % 4 == 0 ? theme::textPrimary : theme::textMuted);
            g.drawText (juce::String (step + 1),
                        juce::Rectangle<float> (gridBounds.getX() + step * w, (float) titleArea.getY(), w, (float) titleArea.getHeight()),
                        juce::Justification::centred);
        }
    }

    void DrumMachinePanel::resized()
    {
        auto area = getLocalBounds().reduced (12, 6);
        titleArea = area.removeFromTop (32);
        clearButton.setBounds (titleArea.withLeft (titleArea.getX() + headerWidth - 70).withWidth (60).reduced (0, 4));

        auto headerColumn = area.removeFromLeft (headerWidth);
        for (auto& h : headers)
            h->setBounds (headerColumn.removeFromTop (rowHeight).withTrimmedRight (12));

        grid->setBounds (area.withHeight (rowHeight * numLanes));
    }
}
