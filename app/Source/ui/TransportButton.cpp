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
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool enabled = isEnabled();
        const bool hot = enabled && (highlighted || down);
        const float radius = 8.0f;

        if (style == Style::primary)
        {
            // Play: always the accent, with a glow (stronger while playing).
            juce::Path shape;
            shape.addRoundedRectangle (bounds.reduced (2.0f), radius);
            if (enabled)
                juce::DropShadow (theme::purple.withAlpha (lit ? 0.75f : 0.55f), 16, {}).drawForPath (g, shape);
            auto fill = theme::purple;
            if (down)       fill = fill.darker (0.15f);
            else if (hot)   fill = fill.brighter (0.15f);
            g.setColour (enabled ? fill : fill.withMultipliedAlpha (0.4f));
            g.fillPath (shape);
        }
        else
        {
            // Surface: a card button. Ghost: transparent inside the transport group.
            // Lit: the accent's dim fill and outline (record: red @ 0.15).
            const bool red = litColour == theme::record;
            juce::Colour fill = style == Style::surface ? theme::bgSurface : juce::Colours::transparentBlack;
            juce::Colour outline = style == Style::surface ? theme::borderSubtle : juce::Colours::transparentBlack;
            if (lit)
            {
                fill = red ? theme::record.withAlpha (0.15f) : theme::purpleDim;
                outline = red ? theme::record.withAlpha (0.6f) : theme::purple;
            }
            else if (hot)
            {
                fill = down ? theme::bgRaised.darker (0.15f) : theme::bgRaised;
                outline = theme::border;
            }
            g.setColour (fill);
            g.fillRoundedRectangle (bounds, radius);
            g.setColour (outline);
            g.drawRoundedRectangle (bounds, radius, theme::borderWidth);
        }

        auto iconColour = style == Style::primary ? theme::textPrimary
                        : fixedIconColour.has_value() ? *fixedIconColour
                        : lit ? litColour
                        : hot ? theme::textPrimary : theme::textSecondary;
        if (! enabled)
            iconColour = style == Style::primary ? iconColour.withAlpha (0.5f) : theme::textMuted;
        g.setColour (iconColour);

        auto iconArea = bounds.withSizeKeepingCentre (16.0f, 16.0f);
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

            case Icon::record:
                g.fillEllipse (iconArea.withSizeKeepingCentre (12.0f, 12.0f));
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

            case Icon::undo:
            case Icon::redo:
            {
                // A half-circle arc over the top; the arrow head points down at the
                // left end (undo) or the right end (redo).
                const bool left = icon == Icon::undo;
                const auto c = iconArea.getCentre().translated (0.0f, 2.5f);
                const float r = 5.5f;
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const float hx = left ? c.x - r : c.x + r;
                juce::Path head;
                head.addTriangle (hx - 3.5f, c.y - 0.5f, hx + 3.5f, c.y - 0.5f, hx, c.y + 4.0f);
                g.fillPath (head);
                break;
            }

            case Icon::menu:
            {
                // Three bars, like a list of commands.
                for (int i = 0; i < 3; ++i)
                    g.fillRoundedRectangle (iconArea.getX() + 1.0f, iconArea.getY() + 2.0f + (float) i * 4.5f, iconArea.getWidth() - 2.0f, 2.0f, 1.0f);
                break;
            }

            case Icon::help:
            {
                // A question mark in a ring.
                g.drawEllipse (iconArea.expanded (1.5f), 1.6f);
                g.setFont (theme::font (12.5f, juce::Font::bold));
                g.drawText ("?", iconArea.expanded (2.0f).translated (0.5f, 0.0f), juce::Justification::centred);
                break;
            }

            case Icon::keyboard:
            {
                // A computer keyboard: outline with two rows of keys and a space bar.
                auto r = iconArea.withSizeKeepingCentre (18.0f, 12.0f);
                g.drawRoundedRectangle (r, 2.0f, 1.5f);
                for (int row = 0; row < 2; ++row)
                    for (int k = 0; k < 4; ++k)
                        g.fillRect (r.getX() + 3.0f + (float) k * 3.4f, r.getY() + 2.5f + (float) row * 2.8f, 1.8f, 1.6f);
                g.fillRect (r.getX() + 5.0f, r.getBottom() - 3.2f, 8.0f, 1.4f);
                break;
            }

            case Icon::settings:
            {
                // Gear: ring, hub and eight teeth.
                const auto c = iconArea.getCentre();
                g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c), 1.8f);
                g.fillEllipse (juce::Rectangle<float> (3.5f, 3.5f).withCentre (c));
                for (int i = 0; i < 8; ++i)
                {
                    const auto angle = (float) i * juce::MathConstants<float>::pi / 4.0f;
                    const juce::Point<float> dir (std::cos (angle), std::sin (angle));
                    g.drawLine ({ c + dir * 5.5f, c + dir * 8.0f }, 2.2f);
                }
                break;
            }
        }
    }
}
