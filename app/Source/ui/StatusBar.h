#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    class StatusBar final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        void setStatus (const juce::String& text, bool isError);
        // Right side: the MIDI inputs and a light that flashes on incoming messages.
        void setMidi (const juce::String& inputs, bool active);

        void paint (juce::Graphics&) override;

    private:
        juce::String text { "Starting..." };
        bool error = false;
        juce::String midiText;
        bool midiActive = false;
    };
}
