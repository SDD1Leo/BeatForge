#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    // BPM display/editor: drag up/down or scroll to change by 1 BPM (Shift for
    // 0.1), double-click to type a value. Range 40-240.
    class TempoField final : public juce::Component,
                             public juce::SettableTooltipClient
    {
    public:
        TempoField();

        void setTempo (double bpm, juce::NotificationType = juce::sendNotificationSync);
        double getTempo() const noexcept { return tempo; }

        std::function<void (double)> onTempoChange;
        std::function<void()> onEditFinished;   // typing done: return focus to the app

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }

    private:
        void showEditor();
        void closeEditor (bool commit);

        double tempo = 120.0;
        double dragStartTempo = 120.0;
        bool dragging = false;
        std::unique_ptr<juce::TextEditor> editor;
    };
}
