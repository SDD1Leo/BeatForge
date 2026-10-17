#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // Project name with a menu chevron; clicking opens the project menu.
    class ProjectButton final : public juce::Button
    {
    public:
        ProjectButton();
        void setProjectName (const juce::String&);

    private:
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        juce::String projectName { "Untitled" };
    };
}
