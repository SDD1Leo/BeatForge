#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    // NEBULA rotary knob: caption above, value (with unit) below. Drag up/down
    // or scroll to change, Shift for fine control, double-click to reset.
    // Drawing lives in BeatForgeLookAndFeel::drawRotarySlider.
    class Knob final : public juce::Component
    {
    public:
        enum class Unit { none, seconds, hertz, percent, decibels };

        Knob (const juce::String& caption, juce::Colour signal);

        void setRange (double min, double max, bool logarithmic, double defaultValue);
        void setUnit (Unit);
        void setValue (double, juce::NotificationType = juce::dontSendNotification);
        double getValue() const { return slider.getValue(); }
        void setTooltip (const juce::String&);

        std::function<void (double)> onChange;

        void resized() override;
        void paint (juce::Graphics&) override;

        static juce::String format (double value, Unit);

    private:
        juce::String caption;
        Unit unit = Unit::none;
        juce::Slider slider;
    };
}
