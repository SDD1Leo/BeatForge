#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace beatforge
{
    // Studio tour: dims the window, spotlights one area at a time and explains
    // it in a card (Back / Next / Skip). Covers its parent and takes the mouse
    // and keyboard while open, so nothing underneath changes during the tour.
    class StudioTour final : public juce::Component
    {
    public:
        struct Step
        {
            juce::String title, body;
            std::function<juce::Rectangle<int>()> target;   // in the parent's coordinates; empty = no spotlight
        };

        explicit StudioTour (std::vector<Step>);

        std::function<void()> onClose;   // after Finish, Skip or Escape

        int getStep() const noexcept { return step; }
        void showStep (int index);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void parentSizeChanged() override;

    private:
        void close();
        juce::Rectangle<int> spotlight() const;
        juce::Rectangle<int> cardBounds() const;
        juce::TextLayout bodyLayout() const;

        std::vector<Step> steps;
        int step = 0;
        juce::TextButton skipButton { "Skip tour" }, backButton { "Back" }, nextButton { "Next" };
        static constexpr int cardWidth = 380, padding = 20;
    };
}
