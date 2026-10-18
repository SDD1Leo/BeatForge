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

    void StatusBar::setMidi (const juce::String& inputs, bool active)
    {
        if (inputs != midiText || active != midiActive)
        {
            midiText = inputs;
            midiActive = active;
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

        if (midiText.isNotEmpty())
        {
            g.setFont (theme::font (12.0f));
            const auto width = juce::jmin (area.getWidth() / 2, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), midiText) + 4);
            auto midi = area.removeFromRight (width + 16);
            g.setColour (midiActive ? theme::accent : theme::control);
            g.fillEllipse (midi.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
            midi.removeFromLeft (8);
            g.setColour (theme::textSecondary);
            g.drawText (midiText, midi, juce::Justification::centredLeft, true);
            area.removeFromRight (16);
        }

        g.setColour (error ? theme::error : theme::ok);
        g.fillEllipse (area.removeFromLeft (8).withSizeKeepingCentre (8, 8).toFloat());
        area.removeFromLeft (8);

        g.setColour (error ? theme::textPrimary : theme::textSecondary);
        g.setFont (theme::font (12.0f));
        g.drawText (text, area, juce::Justification::centredLeft, true);
    }
}
