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

    void StatusBar::showNotice (const juce::String& message, int milliseconds)
    {
        notice = message;
        noticeUntil = juce::Time::getMillisecondCounter() + (juce::uint32) milliseconds;
        repaint();
        juce::Timer::callAfterDelay (milliseconds + 50, [safe = juce::Component::SafePointer<StatusBar> (this)]
        {
            if (safe != nullptr) safe->repaint();
        });
    }

    namespace
    {
        const juce::String separator = juce::String::fromUTF8 ("\xc2\xb7");

        juce::StringArray segmentsOf (const juce::String& text)
        {
            auto parts = juce::StringArray::fromTokens (text, separator, {});
            parts.trim();
            parts.removeEmptyStrings();
            return parts;
        }

        // Segments separated by a muted middle dot; the first one may be bold.
        // MCP segments get the accent dot (spec section 11). Returns the width used.
        int drawSegments (juce::Graphics& g, const juce::StringArray& parts, juce::Rectangle<int> area,
                          bool firstBold, juce::Colour textColour, bool measureOnly = false)
        {
            const auto plain = juce::Font (theme::font (11.0f)), bold = juce::Font (theme::font (11.0f, juce::Font::bold));
            const int gap = 10;
            int x = area.getX();
            for (int i = 0; i < parts.size(); ++i)
            {
                if (i > 0)
                {
                    if (! measureOnly)
                    {
                        g.setColour (theme::textMuted);
                        g.setFont (plain);
                        g.drawText (separator, x, area.getY(), gap * 2, area.getHeight(), juce::Justification::centred, false);
                    }
                    x += gap * 2;
                }
                const bool mcp = parts[i].startsWith ("MCP");
                if (mcp)
                {
                    if (! measureOnly)
                    {
                        g.setColour (parts[i].startsWith ("MCP off") ? theme::textMuted : theme::purple);
                        g.fillEllipse (juce::Rectangle<int> (x, 0, 7, 7).withCentre ({ x + 3, area.getCentreY() }).toFloat());
                    }
                    x += 7 + 6;
                }
                const auto& font = (i == 0 && firstBold) || (mcp && ! parts[i].startsWith ("MCP off")) ? bold : plain;
                const int w = juce::jmin (area.getRight() - x, juce::GlyphArrangement::getStringWidthInt (font, parts[i]) + 2);
                if (! measureOnly && w > 0)
                {
                    g.setFont (font);
                    g.setColour (mcp && ! parts[i].startsWith ("MCP off") ? theme::textPrimary : textColour);
                    g.drawText (parts[i], x, area.getY(), w, area.getHeight(), juce::Justification::centredLeft, true);
                }
                x += juce::jmax (0, w);
            }
            return x - area.getX();
        }
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.setColour (error ? theme::error.withAlpha (0.12f).overlaidWith (theme::bgPanel.withAlpha (0.6f)) : theme::bgPanel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::borderSubtle);
        g.fillRect (0, 0, getWidth(), 1);

        auto area = getLocalBounds().reduced (16, 0);

        if (midiText.isNotEmpty())
        {
            // Right side: MIDI light, inputs, computer keys, MCP status.
            const auto parts = segmentsOf (midiText);
            const int width = juce::jmin (area.getWidth() / 2, drawSegments (g, parts, area, false, theme::textSecondary, true) + 2);
            auto right = area.removeFromRight (width + 13);
            g.setColour (midiActive ? theme::ok : theme::textMuted);
            g.fillEllipse (right.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat());
            right.removeFromLeft (6);
            drawSegments (g, parts, right, false, theme::textSecondary);
            area.removeFromRight (16);
        }

        if (notice.isNotEmpty() && juce::Time::getMillisecondCounter() < noticeUntil)
        {
            g.setColour (theme::accent);
            g.fillEllipse (area.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat());
            area.removeFromLeft (8);
            g.setColour (theme::textPrimary);
            g.setFont (theme::font (11.0f));
            g.drawText (notice, area, juce::Justification::centredLeft, true);
            return;
        }

        g.setColour (error ? theme::error : theme::ok);
        g.fillEllipse (area.removeFromLeft (7).withSizeKeepingCentre (7, 7).toFloat());
        area.removeFromLeft (8);
        drawSegments (g, segmentsOf (text), area, ! error, error ? theme::textPrimary : theme::textSecondary);
    }
}
