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

        switch (icon)
        {
            case Icon::play:
                path.addTriangle (iconArea.getX() + 1.0f, iconArea.getY(),
                                  iconArea.getX() + 1.0f, iconArea.getBottom(),
                                  iconArea.getRight(), iconArea.getCentreY());
                g.fillPath (path);
                break;

            case Icon::stop:
                path.addRoundedRectangle (iconArea.reduced (1.0f), 1.5f);
                g.fillPath (path);
                break;

            case Icon::loop:
            {
                // Rounded loop with an arrow head on the top run.
                auto r = iconArea.withSizeKeepingCentre (16.0f, 10.0f);
                path.addRoundedRectangle (r, 4.0f);
                g.strokePath (path, juce::PathStrokeType (1.6f));
                juce::Path head;
                head.addTriangle (r.getCentreX() + 1.0f, r.getY() - 3.5f,
                                  r.getCentreX() + 1.0f, r.getY() + 3.5f,
                                  r.getCentreX() + 5.0f, r.getY());
                g.fillPath (head);
                break;
            }

            case Icon::metronome:
            {
                // Trapezoid body with a tilted pendulum arm.
                auto r = iconArea.expanded (1.0f);
                path.startNewSubPath (r.getX() + 4.0f, r.getY());
                path.lineTo (r.getRight() - 4.0f, r.getY());
                path.lineTo (r.getRight(), r.getBottom());
                path.lineTo (r.getX(), r.getBottom());
                path.closeSubPath();
                g.strokePath (path, juce::PathStrokeType (1.6f));
                g.drawLine (r.getCentreX(), r.getBottom() - 3.0f, r.getRight() - 2.0f, r.getY() + 1.0f, 1.6f);
                break;
            }
        }
    }
}
