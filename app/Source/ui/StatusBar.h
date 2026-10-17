#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    class StatusBar final : public juce::Component,
                            public juce::SettableTooltipClient
    {
    public:
        void setStatus (const juce::String& text, bool isError);

        void paint (juce::Graphics&) override;

    private:
        juce::String text { "Starting..." };
        bool error = false;
    };
}
