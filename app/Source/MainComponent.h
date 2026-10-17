#pragma once

#include "audio/AudioDeviceController.h"
#include "project/ProjectActions.h"
#include "ui/DrumMachinePanel.h"
#include "ui/InstrumentPanel.h"
#include "ui/LevelMeterComponent.h"
#include "ui/PianoRollPanel.h"
#include "ui/TabBar.h"
#include "ui/StatusBar.h"
#include "ui/TimelineRuler.h"
#include "ui/TransportBar.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

namespace beatforge
{
    // Root content of the main window: header, transport bar, master output
    // panel and status bar.
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

        // App shortcuts. Also called by the window, so they work even when no
        // component has keyboard focus (e.g. right after a dialog closes).
        bool handleShortcut (const juce::KeyPress&);

        TransportBar& getTransportBar() noexcept { return transportBar; }
        ProjectActions& getProjectActions() noexcept { return actions; }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void refreshDeviceStatus();
        void openAudioSettings();


        AudioDeviceController& devices;
        audio::AudioEngine& engine;
        project::Session& session;
        ProjectActions actions;
        int sessionListenerId = 0;
        TransportBar transportBar;
        TimelineRuler ruler;
        DrumMachinePanel drumPanel;
        PianoRollPanel pianoRoll;
        InstrumentPanel instrumentPanel;
        TabBar workspaceTabs { { "Drums", "Piano Roll", "Instrument" } };
        void showTab (int index);
        LevelMeterComponent outputMeter;
        StatusBar statusBar;
        juce::TooltipWindow tooltips { this, 600 };

        juce::Rectangle<int> headerArea, outputCard, workspaceArea;
        juce::String workspaceHint;

        void setWorkspaceHint (const juce::String&);
        juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
