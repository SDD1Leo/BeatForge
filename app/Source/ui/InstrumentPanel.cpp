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
                return theme::pink;
            return theme::purple;
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

        // Spec 7: a segment with a 34 x 14 waveform icon and the wave's name.
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool on = getToggleState();
            auto fill = on ? theme::purpleDim : theme::bgRaised;
            if (! on && highlighted) fill = theme::bgControl;
            if (down) fill = fill.darker (0.15f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (on ? theme::purple : (highlighted ? theme::border : theme::borderSubtle));
            g.drawRoundedRectangle (r, 7.0f, theme::borderWidth);

            g.setFont (theme::font (11.0f, juce::Font::bold));
            const auto label = getName();
            const float labelWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
            const bool showIcon = r.getWidth() >= labelWidth + 34.0f + 30.0f;
            const float contentWidth = labelWidth + (showIcon ? 34.0f + 8.0f : 0.0f);
            auto content = r.withSizeKeepingCentre (contentWidth, r.getHeight());

            if (showIcon)
            {
                auto icon = content.removeFromLeft (34.0f).withSizeKeepingCentre (34.0f, 14.0f);
                content.removeFromLeft (8.0f);
                juce::Path p;
                for (int i = 0; i <= 60; ++i)
                {
                    const float t = (float) i / 60.0f;
                    const float phase = std::fmod (t * 1.5f + 0.0001f, 1.0f);
                    const juce::Point<float> pt (icon.getX() + t * icon.getWidth(),
                                                 icon.getCentreY() - waveShape (wave, phase) * icon.getHeight() * 0.5f);
                    if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                g.setColour (on ? theme::purple : theme::textSecondary);
                g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
            }

            g.setColour (on ? theme::textPrimary : theme::textSecondary);
            g.drawText (label, content, juce::Justification::centredLeft, false);
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

        resetButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);   // ghost
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
    namespace
    {
        // A 3 px curve with a soft glow in the module's colour.
        void strokeWithGlow (juce::Graphics& g, const juce::Path& curve, juce::Colour colour)
        {
            juce::Path stroked;
            juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath (stroked, curve);
            juce::DropShadow (colour.withAlpha (0.55f), 10, {}).drawForPath (g, stroked);
            g.setColour (colour);
            g.fillPath (stroked);
        }

        void fillUnder (juce::Graphics& g, juce::Path curve, juce::Rectangle<float> r, juce::Colour colour, float alpha)
        {
            const auto bounds = curve.getBounds();
            curve.lineTo (bounds.getRight(), r.getBottom());
            curve.lineTo (bounds.getX(), r.getBottom());
            curve.closeSubPath();
            g.setColour (colour.withAlpha (alpha));
            g.fillPath (curve);
        }

        void handle (juce::Graphics& g, juce::Point<float> at, float size, juce::Colour colour)
        {
            const auto r = juce::Rectangle<float> (size, size).withCentre (at);
            g.setColour (theme::bgInset);
            g.fillEllipse (r);
            g.setColour (colour);
            g.drawEllipse (r.reduced (1.0f), 2.0f);
        }

        void dashedVertical (juce::Graphics& g, float x, float y0, float y1, juce::Colour colour, float dash, float gap)
        {
            const float pattern[] { dash, gap };
            g.setColour (colour);
            g.drawDashedLine (juce::Line<float> (x, y0, x, y1), pattern, 2, 1.0f);
        }

        void chip (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
        {
            g.setColour (theme::bgControl);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (theme::textSecondary);
            g.setFont (theme::numericFont (9.0f));
            g.drawText (text, r, juce::Justification::centred, false);
        }

        float chipWidth (const juce::String& text)
        {
            return juce::GlyphArrangement::getStringWidth (juce::Font (theme::numericFont (9.0f)), text) + 16.0f;
        }
    }

    juce::String InstrumentPanel::chipText (int module) const
    {
        if (module == 0) return juce::String (waveNames[(int) value (Param::wave)]).toUpperCase();
        if (module == 1) return "LOW-PASS";
        return type() == audio::InstrumentType::monoBass ? "FILTER PLUCK" : "AMP + FILTER";
    }

    void InstrumentPanel::paintWaveDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto wave = (dsp::Waveform) (int) value (Param::wave);
        r = r.reduced (16.0f, 0.0f);
        const float amplitude = r.getHeight() * 0.36f;

        g.setColour (theme::borderStrong.withAlpha (0.8f));
        g.fillRect (r.getX(), r.getCentreY() - 0.5f, r.getWidth(), 1.0f);

        juce::Path p;
        for (int i = 0; i <= 400; ++i)
        {
            const float t = (float) i / 400.0f;
            const float phase = std::fmod (t * 2.0f, 1.0f);   // two cycles
            const juce::Point<float> pt (r.getX() + t * r.getWidth(), r.getCentreY() - waveShape (wave, phase) * amplitude);
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        fillUnder (g, p, r.withBottom (r.getCentreY() + amplitude), theme::purple, 0.12f);
        strokeWithGlow (g, p, theme::purple);
    }

    void InstrumentPanel::paintFilterDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const double cutoff = value (Param::cutoff), res = value (Param::resonance);
        const double minF = 20.0, maxF = 20000.0, minDb = -36.0, maxDb = 18.0;
        const auto plot = r.withTrimmedBottom (18.0f);

        auto dbToY = [&] (double db) { return plot.getBottom() - (float) ((juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb)) * plot.getHeight(); };
        auto fToX = [&] (double f) { return r.getX() + (float) (std::log (f / minF) / std::log (maxF / minF)) * r.getWidth(); };
        auto dbAt = [&] (double f) { return 20.0 * std::log10 (std::max (1e-6, dsp::StateVariableLowPass::magnitude (f, cutoff, res, 48000.0))); };

        juce::Path curve;
        for (int i = 0; i <= 200; ++i)
        {
            const double f = minF * std::pow (maxF / minF, i / 200.0);
            const juce::Point<float> pt (fToX (f), dbToY (dbAt (f)));
            if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
        }
        fillUnder (g, curve, r, theme::cyan, 0.10f);

        const auto cx = fToX (cutoff);
        dashedVertical (g, cx, r.getY(), r.getBottom(), theme::cyan.withAlpha (0.8f), 4.0f, 4.0f);
        strokeWithGlow (g, curve, theme::cyan);

        // Handle on the curve at the cutoff, with its value beside it.
        const juce::Point<float> at (cx, dbToY (dbAt (cutoff)));
        handle (g, at, 12.0f, theme::cyan);
        const auto text = Knob::format (cutoff, Knob::Unit::hertz);
        const auto w = chipWidth (text);
        auto label = juce::Rectangle<float> (w, 18.0f).withPosition (cx + 10.0f, juce::jmax (r.getY() + 4.0f, at.y - 42.0f));
        if (label.getRight() > r.getRight() - 4.0f)
            label.setX (cx - 10.0f - w);
        chip (g, label, text);

        g.setColour (theme::textMuted);
        g.setFont (theme::numericFont (9.0f));
        for (auto [f, name] : { std::pair { 100.0, "100 Hz" }, std::pair { 1000.0, "1 kHz" }, std::pair { 10000.0, "10 kHz" } })
            g.drawText (name, juce::Rectangle<float> (56.0f, 14.0f).withCentre ({ fToX (f), r.getBottom() - 8.0f }), juce::Justification::centred, false);
    }

    void InstrumentPanel::paintEnvelopeDisplay (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const bool bass = type() == audio::InstrumentType::monoBass;

        // Bass shows its filter pluck (instant attack, decay to zero); the synth its
        // ADSR. Attack, decay and release widths follow their times; the sustain
        // plateau is a fixed 28 % of the width.
        const float a = bass ? 0.0f : value (Param::attack);
        const float d = value (Param::decay);
        const float s = bass ? 0.0f : value (Param::sustain);
        const float rel = bass ? 0.0f : value (Param::release);

        r = r.reduced (16.0f, 0.0f);
        const auto plot = r.withTrimmedTop (28.0f).withTrimmedBottom (24.0f);
        const float plateau = bass ? 0.0f : 0.28f * plot.getWidth();
        const float timed = juce::jmax (1.0e-4f, a + d + rel);
        const float scale = (plot.getWidth() - plateau) / timed;
        const float xa = plot.getX() + a * scale;
        const float xd = xa + d * scale;
        const float xs = xd + plateau;
        const float xr = xs + rel * scale;
        auto y = [&] (float level) { return plot.getBottom() - level * plot.getHeight(); };

        // Linear attack; decay and release ease out like the real exponential curves.
        juce::Path p;
        p.startNewSubPath (plot.getX(), y (0.0f));
        p.lineTo (xa, y (1.0f));
        for (int i = 1; i <= 40; ++i)
        {
            const float u = (float) i / 40.0f;
            p.lineTo (xa + u * (xd - xa), y (s + (1.0f - s) * std::pow (1.0f - u, 3.0f)));
        }
        if (! bass)
        {
            p.lineTo (xs, y (s));
            for (int i = 1; i <= 40; ++i)
            {
                const float u = (float) i / 40.0f;
                p.lineTo (xs + u * (xr - xs), y (s * std::pow (1.0f - u, 3.0f)));
            }
        }

        std::vector<juce::Point<float>> nodes { { plot.getX(), y (0.0f) }, { xa, y (1.0f) }, { xd, y (s) } };
        if (! bass)
        {
            nodes.push_back ({ xs, y (s) });
            nodes.push_back ({ xr, y (0.0f) });
        }

        for (size_t i = 1; i + 1 < nodes.size() + (bass ? 1 : 0); ++i)
            dashedVertical (g, nodes[i].x, nodes[i].y, plot.getBottom(), theme::borderStrong, 3.0f, 4.0f);

        fillUnder (g, p, plot, theme::pink, 0.12f);
        strokeWithGlow (g, p, theme::pink);
        for (auto n : nodes)
            handle (g, n, 11.0f, theme::pink);

        // Segment letters along the bottom.
        g.setColour (theme::textMuted);
        g.setFont (theme::numericFont (10.0f));
        const auto row = r.getBottom() - 10.0f;
        auto letter = [&] (const char* text, float x0, float x1)
        {
            g.drawText (text, juce::Rectangle<float> (20.0f, 14.0f).withCentre ({ (x0 + x1) / 2.0f, row }), juce::Justification::centred, false);
        };
        letter ("A", plot.getX(), xa);
        letter ("D", xa, xd);
        if (! bass)
        {
            letter ("S", xd, xs);
            letter ("R", xs, xr);
        }
    }

    //==============================================================================
    void InstrumentPanel::paint (juce::Graphics& g)
    {
        // The card and its header divider belong to the editor shell; this paints the
        // context (track) and the three modules.
        const auto* track = session.findTrack (trackId());
        if (const auto* selected = session.findTrack (target.getTrack()))
        {
            auto context = headerArea.withLeft (headerStart);
            g.setColour (theme::border);
            g.fillRect (context.removeFromLeft (1).withSizeKeepingCentre (1, 18));
            context.removeFromLeft (16);
            g.setColour (target.colourOf (selected->id));
            g.fillRoundedRectangle (context.removeFromLeft (4).withSizeKeepingCentre (4, 16).toFloat(), 2.0f);
            context.removeFromLeft (10);
            g.setColour (theme::textPrimary);
            g.setFont (theme::font (13.0f, juce::Font::bold));
            const auto name = juce::String::fromUTF8 (selected->name.c_str());
            const auto nameWidth = juce::jmin (220, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 2);
            g.drawText (name, context.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
            context.removeFromLeft (8);
            g.setColour (theme::textMuted);
            g.setFont (theme::captionFont (9.0f));
            g.drawText (selected->type == project::TrackType::drums ? "DRUMS" : "SYNTH", context.removeFromLeft (60),
                        juce::Justification::centredLeft, false);
        }

        if (track == nullptr)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawText ("Select a synth track in the arrangement to edit its instrument.",
                        getLocalBounds().withTrimmedTop (headerArea.getBottom()), juce::Justification::centred);
            return;
        }

        // Header captions beside the instrument buttons and the preset menu.
        g.setColour (theme::textMuted);
        g.setFont (theme::captionFont (9.0f));
        g.drawText ("INSTRUMENT", instrumentCaption, juce::Justification::centredRight, false);
        g.drawText ("PRESET", presetCaption, juce::Justification::centredRight, false);
        g.setColour (theme::border);
        g.fillRect (headerDivider);

        // Modules: card, title row with a dot and a chip, then the display well.
        const juce::Colour accents[] { theme::purple, theme::cyan, theme::pink };
        const char* titles[] { "OSCILLATOR", "FILTER", type() == audio::InstrumentType::monoBass ? "FILTER ENVELOPE" : "ENVELOPE" };
        const juce::Rectangle<int> modules[] { oscArea, filterArea, envArea };
        const juce::Rectangle<int> displays[] { waveDisplay, filterDisplay, envDisplay };
        using Painter = void (InstrumentPanel::*) (juce::Graphics&, juce::Rectangle<float>);
        const Painter painters[] { &InstrumentPanel::paintWaveDisplay, &InstrumentPanel::paintFilterDisplay, &InstrumentPanel::paintEnvelopeDisplay };

        for (int m = 0; m < 3; ++m)
        {
            const auto card = modules[m].toFloat();
            g.setColour (theme::bgSurface);
            g.fillRoundedRectangle (card, 10.0f);
            g.setColour (theme::borderSubtle);
            g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, theme::borderWidth);

            auto title = modules[m].reduced (16, 0).withTrimmedTop (16).withHeight (titleHeight);
            g.setColour (accents[m]);
            g.fillEllipse (title.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            title.removeFromLeft (10);
            const auto text = chipText (m);
            const auto w = chipWidth (text);
            chip (g, title.removeFromRight ((int) std::ceil (w)).toFloat().withSizeKeepingCentre (w, 18.0f), text);
            g.setColour (theme::textSecondary);
            g.setFont (theme::captionFont (11.0f));
            g.drawText (titles[m], title, juce::Justification::centredLeft, true);

            const auto well = displays[m].toFloat();
            g.setColour (theme::bgInset);
            g.fillRoundedRectangle (well, 8.0f);
            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path shape;
                shape.addRoundedRectangle (well, 8.0f);
                g.reduceClipRegion (shape);

                for (int i = 1; i <= 3; ++i)
                {
                    g.setColour (theme::borderSubtle.withAlpha (0.8f));
                    g.fillRect (well.getX(), std::round (well.getY() + well.getHeight() * (float) i / 4.0f), well.getWidth(), 1.0f);
                }
                for (int i = 1; i <= 7; ++i)
                {
                    g.setColour (theme::borderSubtle.withAlpha (0.5f));
                    g.fillRect (std::round (well.getX() + well.getWidth() * (float) i / 8.0f), well.getY(), 1.0f, well.getHeight());
                }
                (this->*painters[m]) (g, well.reduced (0.0f, 10.0f));
            }
            g.setColour (theme::borderSubtle);
            g.drawRoundedRectangle (well.reduced (0.5f), 8.0f, theme::borderWidth);
        }
    }

    void InstrumentPanel::resized()
    {
        // Header (spec 6/7): the editor's tabs sit on the left (drawn by the shell);
        // context after them; instrument buttons, preset menu and Reset on the right.
        auto area = getLocalBounds();
        headerArea = area.removeFromTop (headerHeight).withTrimmedBottom (1);

        auto controls = headerArea.withTrimmedRight (16).withSizeKeepingCentre (headerArea.getWidth() - 16, 30).withX (headerArea.getX());
        resetButton.setBounds (controls.removeFromRight (64));
        controls.removeFromRight (12);
        headerDivider = controls.removeFromRight (1).withSizeKeepingCentre (1, 18);
        controls.removeFromRight (12);
        presetBox.setBounds (controls.removeFromRight (168));
        controls.removeFromRight (8);
        presetCaption = controls.removeFromRight (48);
        controls.removeFromRight (20);
        bassButton.setBounds (controls.removeFromRight (108));
        controls.removeFromRight (8);
        polyButton.setBounds (controls.removeFromRight (108));
        controls.removeFromRight (8);
        instrumentCaption = controls.removeFromRight (76);

        // Body: three equal modules, padding 16, gap 12.
        area = area.reduced (16);
        const int gap = 12;
        const int w = (area.getWidth() - 2 * gap) / 3;
        oscArea = area.removeFromLeft (w);
        area.removeFromLeft (gap);
        filterArea = area.removeFromLeft (w);
        area.removeFromLeft (gap);
        envArea = area;

        auto layoutModule = [&] (juce::Rectangle<int> module, juce::Rectangle<int>& display,
                                 std::initializer_list<Param> params, bool waveRow, int knobGap)
        {
            auto inner = module.reduced (16).withTrimmedTop (titleHeight + 14);
            auto knobsRow = inner.removeFromBottom (juce::jmin (Knob::preferredHeight + 12, inner.getHeight() / 2));
            inner.removeFromBottom (14);

            if (waveRow)
            {
                auto row = inner.removeFromBottom (34);
                inner.removeFromBottom (14);
                const int bw = (row.getWidth() - 3 * 8) / 4;
                for (auto& b : waveButtons)
                {
                    b->setBounds (row.removeFromLeft (bw));
                    row.removeFromLeft (8);
                }
            }
            display = inner;

            std::vector<Knob*> visible;
            for (auto p : params)
                if (audio::usesParam (type(), p))
                    visible.push_back (&knob (p));

            const int count = (int) visible.size();
            const int step = juce::jmin (Knob::preferredWidth + knobGap, count > 0 ? knobsRow.getWidth() / count : 0);
            const int totalW = count * step - (step - juce::jmin (step, Knob::preferredWidth));
            auto x = knobsRow.getX() + (knobsRow.getWidth() - totalW) / 2;
            const auto y = knobsRow.getCentreY() - Knob::preferredHeight / 2;
            for (auto* k : visible)
            {
                k->setBounds (x, y, juce::jmin (step, Knob::preferredWidth), Knob::preferredHeight);
                x += step;
            }
        };

        layoutModule (oscArea, waveDisplay, { Param::level, Param::glide }, true, 28);
        layoutModule (filterArea, filterDisplay, { Param::cutoff, Param::resonance, Param::envAmount }, false, 28);
        layoutModule (envArea, envDisplay, { Param::attack, Param::decay, Param::sustain, Param::release }, false, 20);
    }
}
