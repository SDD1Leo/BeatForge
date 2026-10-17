#include "ui/TransportButton.h"
#include "ui/Theme.h"

namespace beatforge
{
    TransportButton::TransportButton (const juce::String& name, Icon i, juce::Colour lit_)
        : juce::Button (name), icon (i), litColour (lit_)
    {
        setWantsKeyboardFocus (false);   // Space belongs to the transport, not the focused button
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void TransportButton::setLit (bool shouldBeLit)
    {
        if (lit != shouldBeLit)
        {
            lit = shouldBeLit;
            repaint();
        }
    }

    void TransportButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const bool enabled = isEnabled();

        auto fill = lit ? litColour.withAlpha (0.22f) : theme::panelRaised;
        if (enabled && highlighted) fill = fill.brighter (0.08f);
        if (enabled && down)        fill = fill.darker (0.15f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        g.setColour (lit ? litColour : theme::border);
        g.drawRoundedRectangle (bounds, theme::cornerRadius, 1.0f);

        auto iconColour = lit ? litColour : theme::textPrimary;
        if (! enabled) iconColour = theme::textMuted.withAlpha (0.4f);
        g.setColour (iconColour);

        auto iconArea = bounds.withSizeKeepingCentre (14.0f, 14.0f);
        juce::Path path;

        if (icon == Icon::play)
            path.addTriangle (iconArea.getX() + 1.0f, iconArea.getY(),
                              iconArea.getX() + 1.0f, iconArea.getBottom(),
                              iconArea.getRight(), iconArea.getCentreY());
        else
            path.addRoundedRectangle (iconArea.reduced (1.0f), 1.5f);

        g.fillPath (path);
    }
}
