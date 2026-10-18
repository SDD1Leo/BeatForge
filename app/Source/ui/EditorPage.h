#pragma once

#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // A page of the editor panel (Step Sequencer, Piano Roll, Instrument, Mixer).
    // A page covers the whole editor card. Its first 52 px are the shared header:
    // the shell's tabs on the left up to headerStart, then the page's context
    // (track colour, name, type), then the page's own tools on the right.
    class EditorPage : public juce::Component
    {
    public:
        static constexpr int headerHeight = 52;

        void setHeaderStart (int x) { headerStart = x; repaint(); }

    protected:
        juce::Rectangle<int> headerBounds() const { return getLocalBounds().removeFromTop (headerHeight).withTrimmedBottom (1); }

        // Right-side tool row, vertically centred in the header (30 px tall).
        juce::Rectangle<int> toolRow() const
        {
            const auto h = headerBounds();
            return h.withTrimmedRight (16).withSizeKeepingCentre (h.getWidth() - 16, 30).withX (h.getX());
        }

        // Divider, a 4 x 16 colour bar, the name (Semi Bold 13) and a caps tag.
        // Returns where the context ends, for a following chip.
        int paintContext (juce::Graphics& g, juce::Colour colour, const juce::String& name, const juce::String& tag) const
        {
            auto context = headerBounds().withLeft (headerStart);
            g.setColour (theme::border);
            g.fillRect (context.removeFromLeft (1).withSizeKeepingCentre (1, 18));
            context.removeFromLeft (16);
            g.setColour (colour);
            g.fillRoundedRectangle (context.removeFromLeft (4).withSizeKeepingCentre (4, 16).toFloat(), 2.0f);
            context.removeFromLeft (10);
            g.setColour (theme::textPrimary);
            g.setFont (theme::font (13.0f, juce::Font::bold));
            const auto nameWidth = juce::jmin (220, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 2);
            g.drawText (name, context.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
            context.removeFromLeft (8);
            g.setColour (theme::textMuted);
            g.setFont (theme::captionFont (9.0f));
            const auto tagWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), tag) + 2;
            g.drawText (tag, context.removeFromLeft (tagWidth), juce::Justification::centredLeft, false);
            return context.getX();
        }

        // A small Mono chip (bg/control, radius 5); returns its width.
        static int paintChip (juce::Graphics& g, juce::Point<int> leftCentre, const juce::String& text,
                              juce::Colour fill = theme::bgControl, juce::Colour textColour = theme::textSecondary)
        {
            const juce::Font font (theme::numericFont (10.0f));
            const int w = juce::GlyphArrangement::getStringWidthInt (font, text) + 14;
            const auto r = juce::Rectangle<int> (leftCentre.x, leftCentre.y - 10, w, 20).toFloat();
            g.setColour (fill);
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (textColour);
            g.setFont (font);
            g.drawText (text, r, juce::Justification::centred, false);
            return w;
        }

        // A message in the body when there is nothing to edit.
        void paintEmpty (juce::Graphics& g, const juce::String& message) const
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (14.0f));
            g.drawFittedText (message, getLocalBounds().withTrimmedTop (headerHeight).reduced (24), juce::Justification::centred, 2);
        }

        int headerStart = 0;
    };
}
