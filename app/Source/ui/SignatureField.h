#pragma once

#include <beatforge/time/MusicalTime.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    // Time-signature readout segment ("4/4" over "SIG"); click to choose.
    class SignatureField final : public juce::Component,
                                 public juce::SettableTooltipClient
    {
    public:
        SignatureField();

        void setSignature (time::TimeSignature);
        std::function<void (time::TimeSignature)> onChange;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    private:
        time::TimeSignature signature;
        bool menuOpen = false;
    };
}
