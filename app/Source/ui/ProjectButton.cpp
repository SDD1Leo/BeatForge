#include "ui/ProjectButton.h"
#include "ui/Theme.h"

namespace beatforge
{
    ProjectButton::ProjectButton() : juce::Button ("Project")
    {
        setWantsKeyboardFocus (false);
        setTooltip ("Project: New, Open, Save, Save As, Rename");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void ProjectButton::setProjectName (const juce::String& name)
    {
        if (name != projectName)
        {
            projectName = name;
            repaint();
        }
    }

    void ProjectButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? theme::control.darker (0.2f) : (highlighted ? theme::control.brighter (0.08f) : theme::control));
        g.fillRoundedRectangle (bounds, theme::controlRadius);
        g.setColour (highlighted ? theme::borderStrong : theme::border);
        g.drawRoundedRectangle (bounds, theme::controlRadius, theme::borderWidth);

        auto area = getLocalBounds().reduced (12, 0);
        auto chevronArea = area.removeFromRight (12).toFloat().withSizeKeepingCentre (9.0f, 5.0f);

        // Project glyph: a small stack of three bars (a "song").
        auto glyph = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (12.0f, 11.0f);
        g.setColour (theme::violet);
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (glyph.getX(), glyph.getY() + (float) i * 4.0f, glyph.getWidth() - (float) i * 3.0f, 2.4f, 1.2f);
        area.removeFromLeft (8);

        g.setColour (theme::textPrimary);
        g.setFont (theme::font (14.5f, juce::Font::bold));
        g.drawText (projectName, area, juce::Justification::centredLeft, true);

        juce::Path chevron;
        chevron.startNewSubPath (chevronArea.getX(), chevronArea.getY());
        chevron.lineTo (chevronArea.getCentreX(), chevronArea.getBottom());
        chevron.lineTo (chevronArea.getRight(), chevronArea.getY());
        g.setColour (highlighted ? theme::textPrimary : theme::textSecondary);
        g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
