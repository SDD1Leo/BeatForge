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
        // A short message that replaces the device status for a few seconds.
        void showNotice (const juce::String& message, int milliseconds = 4000);

        void paint (juce::Graphics&) override;

    private:
        juce::String text { "Starting..." };
        bool error = false;
        juce::String notice;
        juce::uint32 noticeUntil = 0;
        juce::String midiText;
        bool midiActive = false;
    };
}
