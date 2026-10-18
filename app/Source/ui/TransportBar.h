#pragma once

#include "ui/ProjectButton.h"
#include "ui/SignatureField.h"
#include "ui/TempoField.h"
#include "ui/TransportButton.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    // NEBULA transport row: project + saved state, transport buttons, the
    // readout strip (BPM | SIG | BAR.BEAT.TICK | TIME), beat lights, metronome,
    // loop on/off (the region is set on the arrangement ruler) and audio settings. Settings go through the Session; the
    // playing position is read from the engine for display only.
    class TransportBar final : public juce::Component,
                               public juce::TooltipClient,
                               private juce::Timer
    {
    public:
        TransportBar (audio::AudioEngine&, project::Session&);
        ~TransportBar() override;

        // Play is only possible while an output device is open.
        void setDeviceAvailable (bool available);
        void togglePlayStop();
        void toggleLoop();
        void toggleMetronome();

        // Recording: the bar shows the button; the owner decides what recording means.
        std::function<void()> onRecord;
        void setRecordAvailable (bool available, const juce::String& tooltip);
        void setRecording (bool isRecording);

        // Computer keyboard as a MIDI keyboard (the owner keeps the state).
        std::function<void()> onComputerKeys;
        void setComputerKeysLit (bool lit, const juce::String& tooltip);

        // Save indicator: whether the current unsaved changes are in the recovery file.
        void setAutosave (bool currentIsAutosaved, juce::Time when);
        juce::String getTooltip() override;   // explains the save indicator

        std::function<void()> onOpenAudioSettings;
        std::function<void (juce::Component& anchor)> onProjectMenu;
        std::function<void()> onTextEditFinished;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int minimumWidthForTime = 1270;

    private:
        void timerCallback() override;
        void refreshPosition();
        void refreshFromSession();

        audio::AudioEngine& engine;
        project::Session& session;
        int listenerId = 0;

        ProjectButton projectButton;
        TransportButton undoButton, redoButton, playButton, stopButton, recordButton, loopButton, metronomeButton, keysButton, settingsButton;
        void refreshUndo();
        bool autosaved = false;
        juce::Time autosaveTime;
        TempoField tempoField;
        SignatureField signatureField;

        juce::Rectangle<int> savedArea, stripArea, positionArea, timeArea, beatArea;
        bool showTime = true;

        juce::String barBeatText { "1.1.000" }, timeText { "00:00.000" };
        int currentBeat = 0;   // 1-based beat lit in the indicator, 0 = none
        bool deviceAvailable = false;
    };
}
