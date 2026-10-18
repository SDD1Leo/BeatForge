#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace beatforge
{
    // The project's name; double-click to rename it in place (Enter keeps it,
    // Escape cancels). The project menu is a separate button.
    class ProjectNameField final : public juce::Component,
                                   public juce::SettableTooltipClient
    {
    public:
        ProjectNameField();

        void setProjectName (const juce::String&);
        std::function<bool (const juce::String&)> onRename;   // false = name refused
        std::function<void()> onEditFinished;                 // give keyboard focus back

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

    private:
        void close (bool commit);

        juce::String projectName { "Untitled" };
        std::unique_ptr<juce::TextEditor> editor;
    };
}
