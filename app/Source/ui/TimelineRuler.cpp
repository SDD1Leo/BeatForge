#include "ui/TimelineRuler.h"
#include "ui/Theme.h"

namespace beatforge
{
    TimelineRuler::TimelineRuler (audio::AudioEngine& e) : engine (e)
    {
        startTimerHz (60);
    }

    void TimelineRuler::setLoop (bool enabled, int bars)
    {
        loopOn = enabled;
        loopBars = bars;
        repaint();
    }

    int TimelineRuler::visibleBars() const
    {
        return loopBars >= 8 ? 16 : 8;
    }

    void TimelineRuler::timerCallback()
    {
        const auto ticks = engine.getPositionTicks();
        if (! juce::exactlyEqual (ticks, lastTicks))
        {
            lastTicks = ticks;
            repaint();
        }
    }

    int TimelineRuler::firstVisibleBar (double ticksPerBar) const
    {
        const auto bar = (int) (engine.getPositionTicks() / ticksPerBar);
        return (bar / visibleBars()) * visibleBars();
    }

    float TimelineRuler::tickToX (double tick, double ticksPerBar) const
    {
        const auto origin = firstVisibleBar (ticksPerBar) * ticksPerBar;
        return (float) ((tick - origin) / (ticksPerBar * visibleBars()) * getWidth());
    }

    void TimelineRuler::paint (juce::Graphics& g)
    {
        const auto sig = engine.getTimeSignature();
        const auto perBar = (double) sig.ticksPerBar();
        const auto perBeat = (double) sig.ticksPerBeat();
        const auto bounds = getLocalBounds().toFloat();
        const float rulerHeight = 26.0f;

        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);

        const auto firstBar = firstVisibleBar (perBar);

        // Loop region (rendered as a band across the full height).
        if (loopOn && firstBar == 0)
        {
            const auto x1 = tickToX (perBar * loopBars, perBar);
            g.setColour (theme::violet.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, x1, bounds.getHeight()));
            g.setColour (theme::violet);
            g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, x1, 4.0f));
        }

        // Bars and beats.
        for (int bar = firstBar; bar < firstBar + visibleBars(); ++bar)
        {
            const auto x = tickToX (bar * perBar, perBar);
            g.setColour (theme::border);
            g.drawVerticalLine ((int) x, 0.0f, bounds.getHeight());
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (11.0f));
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 5.0f, 6.0f, 40.0f, 16.0f),
                        juce::Justification::centredLeft);

            for (int beat = 1; beat < sig.numerator; ++beat)
            {
                const auto bx = tickToX (bar * perBar + beat * perBeat, perBar);
                g.setColour (theme::border.withAlpha (0.5f));
                g.drawVerticalLine ((int) bx, rulerHeight - 6.0f, rulerHeight);
            }
        }

        g.setColour (theme::border);
        g.drawHorizontalLine ((int) rulerHeight, 0.0f, bounds.getWidth());

        // Playhead.
        const auto state = engine.getState();
        if (state != audio::TransportState::stopped || engine.getPositionTicks() > 0.0)
        {
            const auto x = tickToX (engine.getPositionTicks(), perBar);
            g.setColour (theme::playhead.withAlpha (0.25f));   // soft halo
            g.fillRect (juce::Rectangle<float> (x - 3.0f, 0.0f, 6.0f, bounds.getHeight()));
            g.setColour (theme::playhead);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, bounds.getHeight()));
            juce::Path head;
            head.addTriangle (x - 6.0f, 0.0f, x + 6.0f, 0.0f, x, 8.0f);
            g.fillPath (head);
        }

        g.setColour (theme::border);
        g.drawRoundedRectangle (bounds.reduced (0.5f), theme::cornerRadius, 1.0f);
    }
}
