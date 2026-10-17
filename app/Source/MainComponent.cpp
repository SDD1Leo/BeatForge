#include "MainComponent.h"
#include "ui/Theme.h"

#include <beatforge/BuildInfo.h>

#include <juce_audio_utils/juce_audio_utils.h>

namespace beatforge
{
    namespace
    {
        juce::String toJuce (std::string_view s) { return juce::String (s.data(), s.size()); }
        const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");
    }

    MainComponent::MainComponent (audio::AudioEngine& engine, AudioDeviceController& d)
        : devices (d), transportBar (engine), ruler (engine), outputMeter (engine.getOutputMeter())
    {
        transportBar.onOpenAudioSettings = [this] { openAudioSettings(); };
        transportBar.onLoopChanged = [this] (bool on, int bars) { ruler.setLoop (on, bars); };
        transportBar.onTextEditFinished = [this] { grabKeyboardFocus(); };

        addAndMakeVisible (transportBar);
        addAndMakeVisible (ruler);
        addAndMakeVisible (outputMeter);
        addAndMakeVisible (statusBar);

        setWantsKeyboardFocus (true);
        devices.addChangeListener (this);
        refreshDeviceStatus();
        startTimer (500);   // dropout counter has no change notification

        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
        devices.removeChangeListener (this);

        if (settingsWindow != nullptr)
            delete settingsWindow.getComponent();
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*) { refreshDeviceStatus(); }
    void MainComponent::timerCallback()                                  { refreshDeviceStatus(); }

    void MainComponent::refreshDeviceStatus()
    {
        const auto s = devices.getStatus();
        transportBar.setDeviceAvailable (s.deviceOpen);

        if (! s.deviceOpen)
        {
            statusBar.setStatus ("Audio unavailable: " + s.error + dot + "open Audio Settings to choose a device", true);
            setWorkspaceHint ("No audio output - open Audio Settings to choose a device");
            return;
        }

        const auto latencyMs = 1000.0 * (s.bufferSize + s.outputLatencySamples) / s.sampleRate;
        auto text = s.typeName + dot + s.deviceName
                  + dot + juce::String (s.sampleRate / 1000.0, 1) + " kHz"
                  + dot + juce::String (s.bufferSize) + " samples (" + juce::String (latencyMs, 1) + " ms)"
                  + dot + juce::String (s.xruns) + (s.xruns == 1 ? " dropout" : " dropouts");

        if (s.error.isNotEmpty())
            text = "Audio error: " + s.error + dot + text;

        statusBar.setStatus (text, s.error.isNotEmpty());
        setWorkspaceHint ("Space: play / stop    M: metronome    L: loop    Drag or scroll the tempo to change it");
    }

    void MainComponent::setWorkspaceHint (const juce::String& hint)
    {
        if (hint != workspaceHint)
        {
            workspaceHint = hint;
            repaint (workspaceArea);
        }
    }

    void MainComponent::openAudioSettings()
    {
        if (settingsWindow != nullptr)
        {
            settingsWindow->toFront (true);
            return;
        }

        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            devices.getDeviceManager(), 0, 0, 1, 2, false, false, true, false);
        selector->setSize (520, 380);

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio Settings";
        options.dialogBackgroundColour = theme::panel;
        options.content.setOwned (selector.release());
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.escapeKeyTriggersCloseButton = true;
        settingsWindow = options.launchAsync();
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::spaceKey)      { transportBar.togglePlayStop();  return true; }
        if (key.isKeyCode ('M') || key.isKeyCode ('m')) { transportBar.toggleMetronome(); return true; }
        if (key.isKeyCode ('L') || key.isKeyCode ('l')) { transportBar.toggleLoop();      return true; }

        return false;
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        g.fillAll (theme::background);

        // Header: wordmark + version.
        g.setColour (theme::panel);
        g.fillRect (headerArea);
        g.setColour (theme::border);
        g.drawHorizontalLine (headerArea.getBottom() - 1, 0.0f, (float) getWidth());

        auto header = headerArea.reduced (16, 0);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (header.removeFromLeft (10).withSizeKeepingCentre (10, 22).toFloat(), 2.0f);
        header.removeFromLeft (10);
        g.setColour (theme::textPrimary);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("BeatForge", header, juce::Justification::centredLeft);
        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("v" + toJuce (versionString()), header, juce::Justification::centredRight);

        // Empty workspace hint until the sequencer/timeline arrive.
        g.setColour (theme::textMuted.withAlpha (0.7f));
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (workspaceHint, workspaceArea.withTrimmedTop (ruler.getBottom() - workspaceArea.getY()),
                    juce::Justification::centred);

        // Master output panel.
        g.setColour (theme::panel);
        g.fillRoundedRectangle (outputCard.toFloat(), theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (outputCard.toFloat().reduced (0.5f), theme::cornerRadius, 1.0f);
        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("MASTER", outputCard.withHeight (34), juce::Justification::centred);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        headerArea = area.removeFromTop (48);
        transportBar.setBounds (area.removeFromTop (60));
        statusBar.setBounds (area.removeFromBottom (26));

        // Master output card, docked right like a mixer's master strip.
        auto body = area.reduced (16);
        outputCard = body.removeFromRight (120);
        body.removeFromRight (16);
        workspaceArea = body;
        ruler.setBounds (body.removeFromTop (64));
        outputMeter.setBounds (outputCard.reduced (16, 0).withTrimmedTop (36).withTrimmedBottom (16));
    }
}
