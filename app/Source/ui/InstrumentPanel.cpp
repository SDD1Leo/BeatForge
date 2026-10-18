#include "ui/InstrumentPanel.h"
#include "ui/Theme.h"

#include <beatforge/dsp/StateVariableFilter.h>

#include <cmath>

namespace beatforge
{
    using audio::Param;

    namespace
    {
        const char* waveNames[] { "Sine", "Saw", "Square", "Triangle" };

        // One cycle of a waveform for drawing (ideal shapes).
        float waveShape (dsp::Waveform w, float phase)
        {
            switch (w)
            {
                case dsp::Waveform::sine:     return std::sin (juce::MathConstants<float>::twoPi * phase);
                case dsp::Waveform::saw:      return 2.0f * phase - 1.0f;
                case dsp::Waveform::square:   return phase < 0.5f ? 1.0f : -1.0f;
                case dsp::Waveform::triangle: return phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
            }
            return 0.0f;
        }

        Knob::Unit unitFor (Param p)
        {
            switch (p)
            {
                case Param::attack: case Param::decay: case Param::release: case Param::glide: return Knob::Unit::seconds;
                case Param::cutoff:                                                              return Knob::Unit::hertz;
                case Param::sustain: case Param::resonance: case Param::envAmount:               return Knob::Unit::percent;
                case Param::level:                                                               return Knob::Unit::decibels;
                case Param::wave:                                                                break;
            }
            return Knob::Unit::none;
        }

        // Section signal colours: oscillator violet, filter cyan, envelope magenta.
        juce::Colour sectionColour (Param p)
        {
            if (p == Param::cutoff || p == Param::resonance || p == Param::envAmount)
                return theme::cyan;
            if (p == Param::attack || p == Param::decay || p == Param::sustain || p == Param::release)
                return theme::magenta;
            return theme::violet;
        }

        juce::String tooltipFor (Param p, audio::InstrumentType t)
        {
            switch (p)
            {
                case Param::attack:    return "Amp envelope attack time";
                case Param::decay:     return t == audio::InstrumentType::monoBass ? "How fast the filter pluck closes"
                                                                                  : "Time to fall from peak to sustain";
                case Param::sustain:   return "Level held while the note is held";
                case Param::release:   return "Fade-out time after the note ends";
                case Param::cutoff:    return "Low-pass filter cutoff (base value before the envelope)";
                case Param::resonance: return "Emphasis at the cutoff frequency";
                case Param::envAmount: return t == audio::InstrumentType::monoBass ? "How far the pluck opens the filter (up to 5 octaves)"
                                                                                  : "How far the envelope opens the filter (up to 4 octaves)";
                case Param::glide:     return "Slide time between overlapping notes";
                case Param::level:     return "Output level";
                case Param::wave:      break;
            }
            return {};
        }
    }

    //==============================================================================
    class InstrumentPanel::WaveButton final : public juce::Button
    {
    public:
        explicit WaveButton (dsp::Waveform w) : juce::Button (waveNames[(int) w]), wave (w)
        {
            setClickingTogglesState (false);
            setWantsKeyboardFocus (false);
            setTooltip (juce::String (waveNames[(int) w]) + " wave");
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            g.setColour (on ? theme::violet.withAlpha (0.25f) : (highlighted ? theme::control.brighter (0.1f) : theme::control));
            if (down) g.setColour (theme::control.darker (0.2f));
            g.fillRoundedRectangle (r, theme::controlRadius);
            g.setColour (on ? theme::violet : (highlighted ? theme::borderStrong : theme::border));
            g.drawRoundedRectangle (r, theme::controlRadius, theme::borderWidth);

            auto icon = r.reduced (8.0f, 9.0f);
            juce::Path p;
            for (int i = 0; i <= 40; ++i)
            {
                const float phase = (float) i / 40.0f;
                const juce::Point<float> pt (icon.getX() + phase * icon.getWidth(),
                                             icon.getCentreY() - waveShape (wave, phase) * icon.getHeight() * 0.5f);
                if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
            }
            g.setColour (on ? theme::textPrimary : theme::textSecondary);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

    private:
        dsp::Waveform wave;
    };

    //==============================================================================
    InstrumentPanel::InstrumentPanel (project::Session& s, Selection& sel) : session (s), target (sel)
    {
        for (auto* b : { &polyButton, &bassButton })
        {
            b->setClickingTogglesState (false);
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (*b);
        }
        polyButton.setTooltip ("Polyphonic synth: chords, leads, pads");
        bassButton.setTooltip ("Monophonic bass: one note at a time, last note wins, optional glide");
        polyButton.onClick = [this] { session.setInstrument (trackId(), audio::InstrumentType::polySynth); };
        bassButton.onClick = [this] { session.setInstrument (trackId(), audio::InstrumentType::monoBass); };

        presetBox.setTooltip ("Factory sounds for this instrument (loading one is a single undo step)");
        presetBox.setWantsKeyboardFocus (false);
        presetBox.onChange = [this]
        {
            const auto index = presetBox.getSelectedId() - 1;
            const auto presets = audio::presetsFor (type());
            if (index >= 0 && index < (int) presets.size())
                session.applyPreset (trackId(), presets[(size_t) index]);
        };
        addAndMakeVisible (presetBox);

        resetButton.setTooltip ("Reset this instrument's controls to their defaults");
        resetButton.setWantsKeyboardFocus (false);
        resetButton.onClick = [this] { session.resetInstrumentParams (trackId(), type()); };
        addAndMakeVisible (resetButton);

        for (int w = 0; w < 4; ++w)
        {
            waveButtons[(size_t) w] = std::make_unique<WaveButton> ((dsp::Waveform) w);
            waveButtons[(size_t) w]->onClick = [this, w] { session.setInstrumentParam (trackId(), type(), Param::wave, (float) w); };
            addAndMakeVisible (*waveButtons[(size_t) w]);
        }

        for (int i = 0; i < audio::numParams; ++i)
        {
            const auto p = (Param) i;
            if (p == Param::wave)
                continue;

            const auto& spec = audio::paramSpec (p);
            auto knobPtr = std::make_unique<Knob> (juce::String (spec.label.data(), spec.label.size()), sectionColour (p));
            knobPtr->setUnit (unitFor (p));
            knobPtr->onChange = [this, p] (double v) { session.setInstrumentParam (trackId(), type(), p, (float) v); };
            addChildComponent (*knobPtr);
            knobs[(size_t) i] = std::move (knobPtr);
        }

        listenerId = session.addListener ([this] (project::Session::Change change)
        {
            using Change = project::Session::Change;
            if (change == Change::instrument || change == Change::replaced || change == Change::tracks)
                refreshFromSession();
        });
        targetListener = target.addListener ([this] { refreshFromSession(); });

        refreshFromSession();
    }

    InstrumentPanel::~InstrumentPanel()
    {
        session.removeListener (listenerId);
        target.removeListener (targetListener);
    }

    project::TrackId InstrumentPanel::trackId() const
    {
        const auto* t = session.findTrack (target.getTrack());
        return t != nullptr && t->type == project::TrackType::instrument ? t->id : 0;
    }

    audio::InstrumentType InstrumentPanel::type() const
    {
        const auto* t = session.findTrack (trackId());
        return t != nullptr ? t->instrument : audio::InstrumentType::polySynth;
    }

    float InstrumentPanel::value (Param p) const { return session.getInstrumentParam (trackId(), type(), p); }

    void InstrumentPanel::refreshFromSession()
    {
        const auto t = type();
        const bool hasTrack = trackId() != 0;
        for (auto* b : { &polyButton, &bassButton, &resetButton })
            b->setVisible (hasTrack);
        presetBox.setVisible (hasTrack);

        // Presets of this instrument; the current sound's name, or "Custom" once tweaked.
        presetBox.clear (juce::dontSendNotification);
        const auto presets = audio::presetsFor (t);
        for (size_t i = 0; i < presets.size(); ++i)
            presetBox.addItem (juce::String (presets[i].name.data(), presets[i].name.size()), (int) i + 1);
        if (const auto* track = session.findTrack (trackId()))
        {
            const auto* match = audio::matchingPreset (t, track->instrumentParams[(size_t) t]);
            if (match != nullptr)
                presetBox.setSelectedId ((int) (match - presets.data()) + 1, juce::dontSendNotification);
            else
                presetBox.setText ("Custom", juce::dontSendNotification);
        }
        for (auto& b : waveButtons)
            b->setVisible (hasTrack);

        polyButton.setToggleState (t == audio::InstrumentType::polySynth, juce::dontSendNotification);
        bassButton.setToggleState (t == audio::InstrumentType::monoBass, juce::dontSendNotification);

        for (int w = 0; w < 4; ++w)
            waveButtons[(size_t) w]->setToggleState ((int) value (Param::wave) == w, juce::dontSendNotification);

        for (int i = 0; i < audio::numParams; ++i)
        {
            const auto p = (Param) i;
            if (p == Param::wave)
                continue;

            const auto& spec = audio::paramSpec (p);
            auto& k = knob (p);
            k.setRange (spec.min, spec.max, spec.logarithmic, audio::defaultValue (t, p));
            k.setValue (value (p), juce::dontSendNotification);
            k.setTooltip (tooltipFor (p, t) + " (double-click: default)");
            k.setVisible (hasTrack && audio::usesParam (t, p));
        }

        resized();
        repaint();
    }

    //==============================================================================
    void InstrumentPanel::paintWaveDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto wave = (dsp::Waveform) (int) value (Param::wave);
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const float t = (float) i / 200.0f;
            const float phase = std::fmod (t * 2.0f, 1.0f);   // two cycles
            const juce::Point<float> pt (r.getX() + t * r.getWidth(), r.getCentreY() - waveShape (wave, phase) * r.getHeight() * 0.38f);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (theme::violet.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (theme::violet);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void InstrumentPanel::paintFilterDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const double cutoff = value (Param::cutoff), res = value (Param::resonance);
        const double minF = 20.0, maxF = 20000.0, minDb = -36.0, maxDb = 24.0;

        auto dbToY = [&] (double db) { return r.getBottom() - (float) ((juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb)) * r.getHeight(); };
        auto fToX = [&] (double f) { return r.getX() + (float) (std::log (f / minF) / std::log (maxF / minF)) * r.getWidth(); };

        // 0 dB reference line and the cutoff marker.
        g.setColour (theme::border.withAlpha (0.7f));
        g.drawHorizontalLine ((int) dbToY (0.0), r.getX(), r.getRight());
        g.setColour (theme::cyan.withAlpha (0.35f));
        g.drawVerticalLine ((int) fToX (cutoff), r.getY(), r.getBottom());

        juce::Path curve, fill;
        for (int i = 0; i <= 120; ++i)
        {
            const double f = minF * std::pow (maxF / minF, i / 120.0);
            const double db = 20.0 * std::log10 (std::max (1e-6, dsp::StateVariableLowPass::magnitude (f, cutoff, res, 48000.0)));
            const juce::Point<float> pt (fToX (f), dbToY (db));
            if (i == 0) { curve.startNewSubPath (pt); fill.startNewSubPath (r.getX(), r.getBottom()); }
            else curve.lineTo (pt);
            fill.lineTo (pt);
        }
        fill.lineTo (r.getRight(), r.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (theme::cyan.withAlpha (0.22f), r.getX(), r.getY(),
                                                 theme::cyan.withAlpha (0.0f), r.getX(), r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (theme::cyan);
        g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void InstrumentPanel::paintEnvelopeDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const bool bass = type() == audio::InstrumentType::monoBass;

        // Bass shows its filter pluck (instant attack, decay to zero); the synth its ADSR.
        const float a = bass ? 0.001f : value (Param::attack);
        const float d = value (Param::decay);
        const float s = bass ? 0.0f : value (Param::sustain);
        const float rel = bass ? 0.0f : value (Param::release);
        const float hold = 0.25f * (a + d + rel + 0.2f);
        const float total = a + d + hold + rel;

        auto x = [&] (float t) { return r.getX() + t / total * r.getWidth(); };
        auto y = [&] (float level) { return r.getBottom() - level * r.getHeight() * 0.9f; };

        juce::Path p;
        p.startNewSubPath (x (0.0f), y (0.0f));
        p.lineTo (x (a), y (1.0f));
        p.lineTo (x (a + d), y (s));
        p.lineTo (x (a + d + hold), y (s));
        p.lineTo (x (total), y (0.0f));

        juce::Path fill (p);
        fill.lineTo (x (0.0f), y (0.0f));
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (theme::magenta.withAlpha (0.22f), r.getX(), r.getY(),
                                                 theme::magenta.withAlpha (0.0f), r.getX(), r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (theme::magenta);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        for (auto pt : { juce::Point<float> (x (a), y (1.0f)), juce::Point<float> (x (a + d), y (s)), juce::Point<float> (x (a + d + hold), y (s)) })
        {
            g.setColour (theme::background);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
            g.setColour (theme::magenta);
            g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt), 1.5f);
        }

        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont (9.0f));
        g.drawText (bass ? "FILTER PLUCK" : "AMP + FILTER", r.toNearestInt().removeFromTop (12).reduced (4, 0),
                    juce::Justification::centredRight);
    }

    //==============================================================================
    void InstrumentPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius, theme::borderWidth);

        // Header: track-colour stripe, caption, track name.
        const auto* track = session.findTrack (trackId());
        auto header = headerArea;
        g.setColour (track != nullptr ? target.colourOf (track->id) : theme::textMuted);
        g.fillRoundedRectangle (header.removeFromLeft (4).toFloat().reduced (0.0f, 6.0f), 2.0f);
        header.removeFromLeft (10);
        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont());
        g.drawText ("INSTRUMENT", header.removeFromLeft (92), juce::Justification::centredLeft);
        g.setColour (theme::textPrimary);
        g.setFont (theme::font (13.5f, juce::Font::bold));
        g.drawText (track != nullptr ? juce::String::fromUTF8 (track->name.c_str()) : juce::String(), header.removeFromLeft (110),
                    juce::Justification::centredLeft);

        if (track == nullptr)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawText ("Select a synth track in the arrangement to edit its instrument.",
                        getLocalBounds().withTrimmedTop (headerArea.getBottom()), juce::Justification::centred);
            return;
        }

        // Sections.
        auto section = [&] (juce::Rectangle<int> area, const char* title)
        {
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (area.toFloat(), theme::cornerRadius);
            g.setColour (theme::border);
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), theme::cornerRadius, theme::borderWidth);
            g.setColour (theme::textSecondary);
            g.setFont (theme::captionFont());
            g.drawText (title, area.reduced (12, 0).removeFromTop (30), juce::Justification::centredLeft);
        };
        section (oscArea, "OSCILLATOR");
        section (filterArea, "FILTER");
        section (envArea, type() == audio::InstrumentType::monoBass ? "FILTER ENVELOPE" : "ENVELOPE");

        for (auto [area, painter] : { std::pair { waveDisplay, &InstrumentPanel::paintWaveDisplay },
                                      std::pair { filterDisplay, &InstrumentPanel::paintFilterDisplay },
                                      std::pair { envDisplay, &InstrumentPanel::paintEnvelopeDisplay } })
        {
            g.setColour (theme::background);
            g.fillRoundedRectangle (area.toFloat(), theme::controlRadius);
            g.setColour (theme::border);
            g.drawRoundedRectangle (area.toFloat().reduced (0.5f), theme::controlRadius, theme::borderWidth);
            g.saveState();
            g.reduceClipRegion (area.reduced (1));
            (this->*painter) (g, area.toFloat().reduced (8.0f, 10.0f));
            g.restoreState();
        }
    }

    void InstrumentPanel::resized()
    {
        auto area = getLocalBounds().reduced (10, 6);
        headerArea = area.removeFromTop (36);

        auto controls = headerArea;
        resetButton.setBounds (controls.removeFromRight (70).reduced (0, 5));
        controls.removeFromRight (16);
        presetBox.setBounds (controls.removeFromRight (150).reduced (0, 5));
        controls.removeFromRight (16);
        bassButton.setBounds (controls.removeFromRight (110).reduced (0, 5));
        controls.removeFromRight (4);
        polyButton.setBounds (controls.removeFromRight (110).reduced (0, 5));

        area.removeFromTop (6);
        const int gap = 10;
        const int w = (area.getWidth() - 2 * gap) / 3;
        oscArea = area.removeFromLeft (w);
        area.removeFromLeft (gap);
        filterArea = area.removeFromLeft (w);
        area.removeFromLeft (gap);
        envArea = area;

        const int knobW = 76, knobH = 92;
        auto layoutSection = [&] (juce::Rectangle<int> sectionArea, juce::Rectangle<int>& display,
                                  std::initializer_list<Param> params, bool waveButtonsRow)
        {
            auto inner = sectionArea.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (12);
            auto knobsRow = inner.removeFromBottom (knobH);

            if (waveButtonsRow)
            {
                auto row = inner.removeFromBottom (40).withTrimmedBottom (8);
                const int bw = (row.getWidth() - 3 * 6) / 4;
                for (auto& b : waveButtons)
                {
                    b->setBounds (row.removeFromLeft (bw));
                    row.removeFromLeft (6);
                }
            }

            display = inner.withTrimmedBottom (8);

            std::vector<Knob*> visible;
            for (auto p : params)
                if (audio::usesParam (type(), p))
                    visible.push_back (&knob (p));

            const int totalW = (int) visible.size() * knobW;
            auto x = knobsRow.getX() + (knobsRow.getWidth() - totalW) / 2;
            for (auto* k : visible)
            {
                k->setBounds (x, knobsRow.getY(), knobW, knobH);
                x += knobW;
            }
        };

        layoutSection (oscArea, waveDisplay, { Param::level, Param::glide }, true);
        layoutSection (filterArea, filterDisplay, { Param::cutoff, Param::resonance, Param::envAmount }, false);
        layoutSection (envArea, envDisplay, { Param::attack, Param::decay, Param::sustain, Param::release }, false);
    }
}
