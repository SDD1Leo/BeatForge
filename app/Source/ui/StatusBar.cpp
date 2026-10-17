#include "ui/StatusBar.h"
#include "ui/Theme.h"

namespace beatforge
{
    void StatusBar::setStatus (const juce::String& newText, bool isError)
    {
        if (newText != text || isError != error)
        {
            text = newText;
            error = isError;
            setTooltip (text);
            repaint();
        }
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.setColour (error ? theme::error.withAlpha (0.12f).overlaidWith (theme::panel.withAlpha (0.6f)) : theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::border);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());

        auto area = getLocalBounds().reduced (16, 0);
        g.setColour (error ? theme::error : theme::ok);
        g.fillEllipse (area.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        area.removeFromLeft (8);

        g.setColour (error ? theme::textPrimary : theme::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (text, area, juce::Justification::centredLeft, true);
    }
}
