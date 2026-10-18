#pragma once

#include "ui/Knob.h"
#include "ui/Selection.h"

#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace beatforge
{
    // NEBULA instrument editor for the selected synth track: instrument choice and the
    // selected instrument's controls in OSCILLATOR / FILTER / ENVELOPE sections.
    // The waveform, filter-response and envelope displays are drawn from the
    // actual parameter values. Only parameters the instrument uses are shown.
    class InstrumentPanel final : public juce::Component
    {
    public:
        InstrumentPanel (project::Session&, Selection&);
        ~InstrumentPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class WaveButton;
        void refreshFromSession();
        project::TrackId trackId() const;   // selected track if it is a synth track, else 0
        audio::InstrumentType type() const;
        float value (audio::Param) const;
        Knob& knob (audio::Param p) { return *knobs[(size_t) p]; }

        void paintWaveDisplay (juce::Graphics&, juce::Rectangle<float>);
        void paintFilterDisplay (juce::Graphics&, juce::Rectangle<float>);
        void paintEnvelopeDisplay (juce::Graphics&, juce::Rectangle<float>);

        project::Session& session;
        Selection& target;
        int listenerId = 0, targetListener = 0;

        juce::TextButton polyButton { "Nebula Poly" }, bassButton { "Nebula Bass" }, resetButton { "Reset" };
        std::array<std::unique_ptr<WaveButton>, 4> waveButtons;
        std::array<std::unique_ptr<Knob>, audio::numParams> knobs;

        juce::Rectangle<int> headerArea, oscArea, filterArea, envArea;
        juce::Rectangle<int> waveDisplay, filterDisplay, envDisplay;
    };
}
