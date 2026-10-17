#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // NEBULA styling for stock JUCE widgets (buttons, combo boxes, sliders,
    // menus, tooltips, dialogs). Custom BeatForge components read the same
    // tokens from Theme.h directly.
    class BeatForgeLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        BeatForgeLookAndFeel();

        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                   bool highlighted, bool down) override;

        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                           int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;

        juce::Font getPopupMenuFont() override;

        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                               float sliderPos, float minSliderPos, float maxSliderPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider&) override;

        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;
    };
}
