#pragma once

#include "ui/ProjectNameField.h"
#include "ui/SignatureField.h"
#include "ui/TempoField.h"
#include "ui/TransportButton.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatforge
{
    // The app header: logo, project menu + name + saved state, undo/redo, the
    // transport group, the readout strip (BPM | SIG | BAR.BEAT.TICK | TIME), beat
    // lights, metronome, computer keys, help, version and audio settings. Loop on/off
    // here; the region is set on the arrangement ruler. Settings go through the Session; the
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
        std::function<void (juce::Component& anchor)> onProjectMenu, onHelpMenu;
        std::function<void()> onTextEditFinished;

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int height = 68;

        // Areas the studio tour points at (in this component's coordinates).
        juce::Rectangle<int> getProjectArea() const    { return menuButton.getBounds().getUnion (redoButton.getBounds()); }
        juce::Rectangle<int> getTransportArea() const  { return groupArea; }
        juce::Rectangle<int> getReadoutArea() const    { return stripArea; }
        juce::Rectangle<int> getTogglesArea() const    { return beatArea.getUnion (helpButton.getBounds()); }

    private:
        void timerCallback() override;
        void refreshPosition();
        void refreshFromSession();

        audio::AudioEngine& engine;
        project::Session& session;
        int listenerId = 0;

        ProjectNameField projectName;
        TransportButton menuButton, helpButton, undoButton, redoButton, playButton, stopButton, recordButton, loopButton, metronomeButton, keysButton, settingsButton;
        void refreshUndo();
        bool autosaved = false;
        juce::Time autosaveTime;
        TempoField tempoField;
        SignatureField signatureField;

        juce::Rectangle<int> brandArea, projectArea, savedArea, groupArea, stripArea, positionArea, timeArea, beatArea, versionArea;
        bool showTime = true, showWordmark = true, showVersion = true;

        juce::String barBeatText { "1.1.000" }, timeText { "00:00.000" };
        int currentBeat = 0;   // 1-based beat lit in the indicator, 0 = none
        bool deviceAvailable = false;
    };
}
