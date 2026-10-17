#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // Root content of the main window. Phase 0 only shows identity and status;
    // panels (transport, timeline, mixer...) are added by later phases.
    class MainComponent final : public juce::Component
    {
    public:
        MainComponent();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::Rectangle<int> headerArea, statusArea, bodyArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
