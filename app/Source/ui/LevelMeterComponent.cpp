#include "ui/LevelMeterComponent.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr float decayDbPerSecond = 26.0f;
        constexpr double holdMs = 1500.0;
        float peakDbOf (float peak) { return audio::LevelMeter::toDecibels (peak, LevelMeterComponent::minDb); }
    }

    LevelMeterComponent::LevelMeterComponent (audio::LevelMeter& m) : meter (m)
    {
        setTooltip ("Output peak level (dBFS). Click to reset the clip lights and the peak readout.");
        lastTickMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (30);
    }

    void LevelMeterComponent::timerCallback()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto elapsedSeconds = (float) ((now - lastTickMs) / 1000.0);
        lastTickMs = now;
        const auto before = channels;
        const auto peakBefore = maxPeakDb;

        for (int ch = 0; ch < (int) channels.size(); ++ch)
        {
            auto& c = channels[(size_t) ch];
            const auto peak = meter.takePeak (ch);
            const auto peakDb = audio::LevelMeter::toDecibels (peak, minDb);

            c.displayDb = juce::jmax (peakDb, c.displayDb - decayDbPerSecond * elapsedSeconds);

            if (peakDb >= c.holdDb || now > c.holdUntilMs)
            {
                c.holdDb = juce::jmax (peakDb, c.displayDb);
                c.holdUntilMs = now + holdMs;
            }

            if (peak >= 1.0f)
                c.clipped = true;
            maxPeakDb = juce::jmax (maxPeakDb, peakDbOf (peak));
        }

        // Repaint only what can be seen and has changed (an idle meter costs nothing).
        auto changed = [] (const Channel& a, const Channel& b)
        {
            return std::abs (a.displayDb - b.displayDb) > 0.05f || std::abs (a.holdDb - b.holdDb) > 0.05f || a.clipped != b.clipped;
        };
        if (isShowing() && (changed (channels[0], before[0]) || changed (channels[1], before[1]) || std::abs (maxPeakDb - peakBefore) > 0.05f))
            repaint();
    }

    float LevelMeterComponent::dbToY (float db, juce::Rectangle<float> bar) const
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
        return bar.getBottom() - proportion * bar.getHeight();
    }

    void LevelMeterComponent::paint (juce::Graphics& g)
    {
        // Master layout (spec section 10): dB scale on the left, two 26 px wells
        // with clip lights, L/R labels and the peak readout below. Without the
        // scale (mixer strips) the two bars fill the component.
        auto area = getLocalBounds().toFloat();
        if (showScale)
        {
            readoutArea = area.removeFromBottom (54.0f);
            area.removeFromBottom (10.0f);
        }
        auto labels = showScale ? area.removeFromBottom (18.0f) : juce::Rectangle<float>();

        const float wellWidth = showScale ? 26.0f : (area.getWidth() - 2.0f) / 2.0f;
        const float firstX = showScale ? area.getX() + 40.0f : area.getX();
        const float pitch = showScale ? 32.0f : wellWidth + 2.0f;
        const float inset = showScale ? 2.0f : 1.0f;

        // Bars run from below the clip light to the bottom of the well.
        const auto well0 = juce::Rectangle<float> (firstX, area.getY(), wellWidth, area.getHeight());
        const auto barArea = well0.reduced (inset).withTrimmedTop (showScale ? 12.0f : 8.0f);

        if (showScale)
        {
            g.setFont (theme::numericFont (9.0f));
            for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
            {
                const auto y = dbToY (db, barArea);
                g.setColour (theme::textMuted);
                g.drawText (juce::String ((int) db), juce::Rectangle<float> (area.getX(), y - 6.0f, 26.0f, 12.0f),
                            juce::Justification::centredRight);
                g.setColour (theme::borderStrong);
                g.fillRect (area.getX() + 29.0f, y - 0.5f, 6.0f, 1.0f);
            }
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& c = channels[(size_t) ch];
            const auto well = well0.translated ((float) ch * pitch, 0.0f);
            const float radius = showScale ? 4.0f : 2.0f;

            g.setColour (theme::bgInset);
            g.fillRoundedRectangle (well, radius);
            g.setColour (theme::borderSubtle);
            g.drawRoundedRectangle (well.reduced (0.5f), radius, 1.0f);

            const auto bar = barArea.translated ((float) ch * pitch, 0.0f);

            // Cyan at the bottom, violet, pink at the top: fixed to the scale so a
            // colour always means the same level.
            if (c.displayDb > minDb)
            {
                g.setGradientFill (theme::meterGradient (bar));
                g.fillRoundedRectangle (bar.withTop (dbToY (c.displayDb, bar)), showScale ? 3.0f : 1.0f);
            }

            if (c.holdDb > minDb)
            {
                g.setColour (theme::textPrimary.withAlpha (0.9f));
                g.fillRect (bar.getX(), dbToY (c.holdDb, bar) - 1.0f, bar.getWidth(), 2.0f);
            }

            const auto light = juce::Rectangle<float> (bar.getX(), well.getY() + inset + 1.0f, bar.getWidth(), showScale ? 6.0f : 4.0f);
            g.setColour (c.clipped ? theme::error : theme::bgControl);
            g.fillRoundedRectangle (light, 1.5f);

            if (showScale)
            {
                g.setColour (theme::textMuted);
                g.setFont (theme::font (10.0f, juce::Font::bold));
                g.drawText (ch == 0 ? "L" : "R", labels.withX (well.getX()).withWidth (well.getWidth()), juce::Justification::centred);
            }
        }

        if (showScale)
        {
            g.setColour (theme::bgInset);
            g.fillRoundedRectangle (readoutArea, 8.0f);
            g.setColour (theme::borderSubtle);
            g.drawRoundedRectangle (readoutArea.reduced (0.5f), 8.0f, 1.0f);
            auto r = readoutArea.reduced (8.0f);
            auto caption = r.removeFromBottom (11.0f);
            g.setColour (theme::textPrimary);
            g.setFont (theme::numericFont (18.0f, juce::Font::bold));
            g.drawText (maxPeakDb > minDb ? juce::String (maxPeakDb, 1) : juce::String ("-inf"), r, juce::Justification::centred);
            g.setColour (theme::textMuted);
            g.setFont (theme::captionFont (9.0f));
            g.drawText ("dB PEAK", caption, juce::Justification::centred);
        }
    }

    void LevelMeterComponent::mouseDown (const juce::MouseEvent&)
    {
        for (auto& c : channels)
            c.clipped = false;
        maxPeakDb = minDb;
        repaint();
    }
}
