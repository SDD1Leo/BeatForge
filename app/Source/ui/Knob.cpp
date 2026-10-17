#include "ui/Knob.h"
#include "ui/Theme.h"

#include <cmath>

namespace beatforge
{
    Knob::Knob (const juce::String& c, juce::Colour signal) : caption (c)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 72, 16);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setMouseDragSensitivity (180);
        slider.setVelocityBasedMode (false);
        slider.setWantsKeyboardFocus (false);
        slider.setColour (juce::Slider::rotarySliderFillColourId, signal);
        slider.setColour (juce::Slider::textBoxTextColourId, theme::textPrimary);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.textFromValueFunction = [this] (double v) { return format (v, unit); };
        slider.onValueChange = [this] { if (onChange) onChange (slider.getValue()); };
        addAndMakeVisible (slider);
    }

    void Knob::setRange (double min, double max, bool logarithmic, double defaultValue)
    {
        juce::NormalisableRange<double> range (min, max);
        if (logarithmic)
            range.setSkewForCentre (std::sqrt (min * max));   // equal travel per octave / per time ratio
        slider.setNormalisableRange (range);
        slider.setDoubleClickReturnValue (true, defaultValue);
    }

    void Knob::setUnit (Unit u)
    {
        unit = u;
        slider.updateText();
    }

    void Knob::setValue (double v, juce::NotificationType n) { slider.setValue (v, n); }
    void Knob::setTooltip (const juce::String& t)             { slider.setTooltip (t); }

    juce::String Knob::format (double v, Unit u)
    {
        switch (u)
        {
            case Unit::seconds:  return v < 1.0 ? juce::String (v * 1000.0, v < 0.1 ? 1 : 0) + " ms" : juce::String (v, 2) + " s";
            case Unit::hertz:    return v < 1000.0 ? juce::String ((int) std::round (v)) + " Hz" : juce::String (v / 1000.0, 2) + " kHz";
            case Unit::percent:  return juce::String ((int) std::round (v * 100.0)) + "%";
            case Unit::decibels: return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
            case Unit::none:     break;
        }
        return juce::String (v, 2);
    }

    void Knob::paint (juce::Graphics& g)
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont (9.5f));
        g.drawText (caption.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centred);
    }

    void Knob::resized()
    {
        slider.setBounds (getLocalBounds().withTrimmedTop (14));
    }
}
