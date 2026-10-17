#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // Square icon button for the transport bar. "Lit" shows an active state
    // (Play while the transport runs, or a toggle that is on).
    class TransportButton final : public juce::Button
    {
    public:
        enum class Icon { play, stop, loop, metronome };

        TransportButton (const juce::String& name, Icon, juce::Colour litColour);

        void setLit (bool shouldBeLit);

    private:
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

        Icon icon;
        juce::Colour litColour;
        bool lit = false;
    };
}
