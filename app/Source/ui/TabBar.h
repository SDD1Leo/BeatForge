#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    // NEBULA tab strip: text tabs, the active one bright with a violet underline.
    class TabBar final : public juce::Component
    {
    public:
        explicit TabBar (juce::StringArray tabNames);

        void setCurrentTab (int index, juce::NotificationType = juce::sendNotificationSync);
        int getCurrentTab() const noexcept { return current; }
        void setTabName (int index, const juce::String& name) { if (names[index] != name) { names.set (index, name); repaint(); } }

        std::function<void (int)> onChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        int getPreferredWidth() const;

    private:
        juce::Rectangle<int> tabBounds (int index) const;
        int tabAt (juce::Point<int>) const;

        juce::StringArray names;
        int current = 0, hovered = -1;
    };
}
