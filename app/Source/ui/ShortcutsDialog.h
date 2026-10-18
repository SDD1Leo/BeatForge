#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // NEBULA overview of every keyboard shortcut (F1), grouped by area.
    class ShortcutsDialog final : public juce::Component
    {
    public:
        ShortcutsDialog();
        void paint (juce::Graphics&) override;
    };
}
