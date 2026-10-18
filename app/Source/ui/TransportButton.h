#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace beatforge
{
    // Square icon button for the transport bar. "Lit" shows an active state
    // (Play while the transport runs, or a toggle that is on).
    class TransportButton final : public juce::Button
    {
    public:
        enum class Icon { play, stop, record, loop, metronome, keyboard, settings, undo, redo, menu, help };

        TransportButton (const juce::String& name, Icon, juce::Colour litColour);

        void setLit (bool shouldBeLit);

        // surface: a card button (bg/surface). ghost: transparent, for buttons inside
        // the transport group. primary: the accent-filled Play button with a glow.
        enum class Style { surface, ghost, primary };
        void setStyle (Style s) { style = s; repaint(); }
        void setIconColour (juce::Colour c) { fixedIconColour = c; repaint(); }

    private:
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

        Icon icon;
        juce::Colour litColour;
        bool lit = false;
        Style style = Style::surface;
        std::optional<juce::Colour> fixedIconColour;
    };
}
