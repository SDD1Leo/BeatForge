#pragma once

#include "audio/AudioDeviceController.h"
#include "ui/LevelMeterComponent.h"
#include "ui/StatusBar.h"
#include "ui/TransportBar.h"

#include <beatforge/audio/AudioEngine.h>

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
        MainComponent (audio::AudioEngine&, AudioDeviceController&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        TransportBar& getTransportBar() noexcept { return transportBar; }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void refreshDeviceStatus();
        void openAudioSettings();
        void setWorkspaceHint (const juce::String&);

        AudioDeviceController& devices;
        TransportBar transportBar;
        LevelMeterComponent outputMeter;
        StatusBar statusBar;
        juce::TooltipWindow tooltips { this, 600 };

        juce::Rectangle<int> headerArea, outputCard, workspaceArea;
        juce::String workspaceHint;
        juce::Component::SafePointer<juce::DialogWindow> settingsWindow;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
