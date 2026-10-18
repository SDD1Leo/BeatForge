#pragma once

#include "audio/AudioDeviceController.h"
#include "input/ComputerKeyboard.h"
#include "ipc/AiBridge.h"
#include "project/Autosave.h"
#include "project/ProjectActions.h"
#include "ui/ArrangementView.h"
#include "ui/DrumMachinePanel.h"
#include "ui/ExportAudioDialog.h"
#include "ui/ProjectBrowser.h"
#include "ui/InstrumentPanel.h"
#include "ui/LevelMeterComponent.h"
#include "ui/MixerPanel.h"
#include "ui/PianoRollPanel.h"
#include "ui/TabBar.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>
#include <beatforge/project/TakeRecorder.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // Root content of the main window: header, transport bar, the arrangement
    // (main screen), the editor for the selected clip/track below it, master
    // output panel and status bar.
    class MainComponent final : public juce::Component,
                                private juce::ChangeListener,
                                private juce::Timer
    {
    public:
        MainComponent (audio::AudioEngine&, AudioDeviceController&, project::Session&, juce::PropertiesFile& settings);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;

        // App shortcuts. Also called by the window, so they work even when no
        // component has keyboard focus (e.g. right after a dialog closes).
        bool handleShortcut (const juce::KeyPress&);

        TransportBar& getTransportBar() noexcept { return transportBar; }

        // Starts recording into the selected synth track (starting playback if
        // needed), or punches out while recording.
        void toggleRecord();
        void toggleComputerKeys();
        // The MCP bridge (local socket). Returns false with a reason if it could not start.
        bool startAiBridge (std::string& error);
        // Offers to restore work left by a crash, then starts autosaving.
        void startAutosave();
        ComputerKeyboard& getComputerKeys() noexcept { return computerKeys; }
        ProjectActions& getProjectActions() noexcept { return actions; }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void refreshDeviceStatus();
        void updateLiveInput();      // live MIDI plays the selected (or recording) synth track
        void pollRecording();
        void openAudioSettings();
        void openExportAudio();
        void openProjectBrowser();
        void showShortcuts();
        void showGuide();
        juce::Component::SafePointer<juce::DialogWindow> guideWindow;
        juce::Component::SafePointer<juce::DialogWindow> browserWindow;
        juce::Component::SafePointer<juce::DialogWindow> exportWindow;


        AudioDeviceController& devices;
        audio::AudioEngine& engine;
        project::Session& session;
        juce::PropertiesFile& appSettings;
        ProjectActions actions;
        ComputerKeyboard computerKeys { engine };
        Autosave autosave { session, appSettings };
        std::unique_ptr<AiBridge> aiBridge;
        void refreshComputerKeys();
        int sessionListenerId = 0;
        TransportBar transportBar;
        Selection selection;
        ArrangementView arrangement;
        DrumMachinePanel drumPanel;
        PianoRollPanel pianoRoll;
        InstrumentPanel instrumentPanel;
        MixerPanel mixer;
        TabBar workspaceTabs { { "Step Sequencer", "Instrument", "Mixer" } };
        int selectionListenerId = 0;
        project::TakeRecorder recorder { session };
        bool deviceOpen = false, takeWasActive = false;
        juce::String midiInputsText, computerKeysText;
        std::uint32_t lastMidiCount = 0;
        int midiLightTicks = 0, slowTicks = 0;
        void showTab (int index);           // 0 = clip editor (by track type), 1 = instrument, 2 = mixer
        LevelMeterComponent outputMeter;
        StatusBar statusBar;
        juce::TooltipWindow tooltips { this, 600 };

        juce::Rectangle<int> outputCard, workspaceArea, editorCard;
        static constexpr int editorHeaderHeight = 52;
        juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
