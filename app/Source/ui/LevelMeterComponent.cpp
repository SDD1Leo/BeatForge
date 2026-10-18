#include "ui/LevelMeterComponent.h"
#include "ui/Theme.h"

namespace beatforge
{
    namespace
    {
        constexpr float decayDbPerSecond = 26.0f;
        constexpr double holdMs = 1500.0;
        constexpr int scaleWidth = 28;
    }

    LevelMeterComponent::LevelMeterComponent (audio::LevelMeter& m) : meter (m)
    {
        setTooltip ("Output peak level (dBFS). Click to reset the clip lights.");
        lastTickMs = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (30);
    }

    void LevelMeterComponent::timerCallback()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto elapsedSeconds = (float) ((now - lastTickMs) / 1000.0);
        lastTickMs = now;
        const auto before = channels;

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
        }

        // Repaint only what can be seen and has changed (an idle meter costs nothing).
        auto changed = [] (const Channel& a, const Channel& b)
        {
            return std::abs (a.displayDb - b.displayDb) > 0.05f || std::abs (a.holdDb - b.holdDb) > 0.05f || a.clipped != b.clipped;
        };
        if (isShowing() && (changed (channels[0], before[0]) || changed (channels[1], before[1])))
            repaint();
    }

    float LevelMeterComponent::dbToY (float db, juce::Rectangle<float> bar) const
    {
        const auto proportion = juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
        return bar.getBottom() - proportion * bar.getHeight();
    }

    void LevelMeterComponent::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().toFloat();
        auto clipRow = area.removeFromTop (10.0f);
        area.removeFromTop (4.0f);
        if (showScale)
            area.removeFromBottom (6.0f);   // room for the bottom scale label
        auto scale = area.removeFromLeft (showScale ? (float) scaleWidth : 0.0f);

        const float gap = showScale ? 4.0f : 2.0f;
        const float barWidth = (area.getWidth() - gap) / 2.0f;

        // Scale ticks.
        g.setFont (theme::font (10.0f));
        for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f })
        {
            if (! showScale)
                break;
            const auto y = dbToY (db, area);
            g.setColour (theme::textSecondary);
            g.drawText (juce::String ((int) db), scale.withY (y - 6.0f).withHeight (12.0f).withTrimmedRight (4.0f),
                        juce::Justification::centredRight);
            g.setColour (theme::border.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& c = channels[(size_t) ch];
            auto bar = juce::Rectangle<float> (area.getX() + (float) ch * (barWidth + gap), area.getY(),
                                               barWidth, area.getHeight());

            g.setColour (theme::background);
            g.fillRoundedRectangle (bar, 2.0f);

            // Cyan body, violet then magenta near the top, red at 0 dBFS - fixed
            // to the dB scale so a colour always means the same level.
            // Stop positions run 0 (0 dB, top) to 1 (minDb, bottom).
            juce::ColourGradient gradient (theme::error, 0.0f, dbToY (0.0f, bar),
                                           theme::meterLow, 0.0f, dbToY (minDb, bar), false);
            gradient.addColour (-3.0 / minDb, theme::meterHigh);
            gradient.addColour (-12.0 / minDb, theme::meterMid);
            gradient.addColour (-24.0 / minDb, theme::meterLow);

            const auto top = dbToY (c.displayDb, bar);
            if (c.displayDb > minDb)
            {
                g.setGradientFill (gradient);
                g.fillRect (bar.withTop (top));
            }

            if (c.holdDb > minDb)
            {
                g.setColour (theme::textPrimary);
                g.fillRect (bar.getX(), dbToY (c.holdDb, bar) - 1.0f, bar.getWidth(), 2.0f);
            }

            auto light = juce::Rectangle<float> (bar.getX(), clipRow.getY(), bar.getWidth(), clipRow.getHeight());
            g.setColour (c.clipped ? theme::error : theme::control);
            g.fillRoundedRectangle (light, 2.0f);
        }
    }

    void LevelMeterComponent::mouseDown (const juce::MouseEvent&)
    {
        for (auto& c : channels)
            c.clipped = false;
        repaint();
    }
}
