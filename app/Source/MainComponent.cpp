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

    MainComponent::MainComponent (audio::AudioEngine& e, AudioDeviceController& d,
                                  project::Session& s, juce::PropertiesFile& settings)
        : devices (d), engine (e), session (s), actions (s, settings, *this),
          transportBar (e, s), selection (s), arrangement (e, s, selection), drumPanel (e, s, selection),
          pianoRoll (e, s, selection), instrumentPanel (s, selection), outputMeter (e.getOutputMeter())
    {
        transportBar.onOpenAudioSettings = [this] { openAudioSettings(); };
        transportBar.onProjectMenu = [this] (juce::Component& anchor) { actions.showMenu (anchor); };
        transportBar.onTextEditFinished = [this] { grabKeyboardFocus(); };
        drumPanel.onTextEditFinished = [this] { grabKeyboardFocus(); };
        arrangement.onTextEditFinished = [this] { grabKeyboardFocus(); };
        arrangement.onOpenEditor = [this] { showTab (0); };

        // The clip editor follows the selected track's kind.
        selectionListenerId = selection.addListener ([this] { showTab (workspaceTabs.getCurrentTab()); });

        addAndMakeVisible (transportBar);
        addAndMakeVisible (arrangement);
        addChildComponent (drumPanel);
        addChildComponent (pianoRoll);
        addChildComponent (instrumentPanel);
        addAndMakeVisible (workspaceTabs);
        workspaceTabs.onChange = [this] (int index) { showTab (index); };
        addAndMakeVisible (outputMeter);
        addAndMakeVisible (statusBar);

        setWantsKeyboardFocus (true);
        devices.addChangeListener (this);
        refreshDeviceStatus();
        startTimer (500);   // dropout counter has no change notification
        showTab (0);

        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
        selection.removeListener (selectionListenerId);
        devices.removeChangeListener (this);

        if (settingsWindow != nullptr)
            delete settingsWindow.getComponent();
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*) { refreshDeviceStatus(); }
    void MainComponent::timerCallback()
    {
        refreshDeviceStatus();
        engine.collectGarbage();   // free note schedules the audio thread has finished with
    }

    void MainComponent::showTab (int index)
    {
        const auto* track = session.findTrack (selection.getTrack());
        const bool drums = track == nullptr || track->type == project::TrackType::drums;
        workspaceTabs.setTabName (0, drums ? "Step Sequencer" : "Piano Roll");
        workspaceTabs.setCurrentTab (index, juce::dontSendNotification);
        drumPanel.setVisible (index == 0 && drums);
        pianoRoll.setVisible (index == 0 && ! drums);
        instrumentPanel.setVisible (index == 1);
        resized();
        repaint (workspaceArea);
    }

    void MainComponent::refreshDeviceStatus()
    {
        const auto s = devices.getStatus();
        transportBar.setDeviceAvailable (s.deviceOpen);

        if (! s.deviceOpen)
        {
            statusBar.setStatus ("Audio unavailable: " + s.error + dot + "open Audio Settings to choose a device", true);
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
        return handleShortcut (key);
    }

    bool MainComponent::handleShortcut (const juce::KeyPress& key)
    {
        const auto ctrl = juce::ModifierKeys::commandModifier;
        if (key == juce::KeyPress ('n', ctrl, 0))  { actions.newProject();      return true; }
        if (key == juce::KeyPress ('o', ctrl, 0))  { actions.openWithChooser(); return true; }
        if (key == juce::KeyPress ('s', ctrl, 0))  { actions.save();            return true; }
        if (key == juce::KeyPress ('s', ctrl | juce::ModifierKeys::shiftModifier, 0)
            || key == juce::KeyPress ('S', ctrl | juce::ModifierKeys::shiftModifier, 0)) { actions.saveAs(); return true; }

        if (key == juce::KeyPress ('z', ctrl, 0))  { session.undo(); return true; }
        if (key == juce::KeyPress ('y', ctrl, 0)
            || key == juce::KeyPress ('z', ctrl | juce::ModifierKeys::shiftModifier, 0)
            || key == juce::KeyPress ('Z', ctrl | juce::ModifierKeys::shiftModifier, 0)) { session.redo(); return true; }
        if (key == juce::KeyPress ('d', ctrl, 0))  { arrangement.duplicateSelectedClip(); return true; }

        if (key == juce::KeyPress ('1', ctrl, 0))  { showTab (0); return true; }
        if (key == juce::KeyPress ('2', ctrl, 0))  { showTab (1); return true; }

        if (key == juce::KeyPress::spaceKey)      { transportBar.togglePlayStop();  return true; }
        // Single-letter shortcuts only without modifiers (Ctrl+L etc. stay free).
        if (key == juce::KeyPress ('m') || key == juce::KeyPress ('M')) { transportBar.toggleMetronome(); return true; }
        if (key == juce::KeyPress ('l') || key == juce::KeyPress ('L')) { transportBar.toggleLoop();      return true; }

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

        // Wordmark: a small waveform glyph in the violet-to-magenta signal colours.
        auto header = headerArea.reduced (16, 0);
        auto glyph = header.removeFromLeft (22).toFloat().withSizeKeepingCentre (22.0f, 22.0f);
        const float bars[] { 0.35f, 0.7f, 1.0f, 0.55f, 0.85f, 0.4f };
        juce::ColourGradient signal (theme::violet, glyph.getX(), 0.0f, theme::magenta, glyph.getRight(), 0.0f, false);
        g.setGradientFill (signal);
        for (int i = 0; i < 6; ++i)
        {
            const float h = glyph.getHeight() * bars[i];
            g.fillRoundedRectangle (glyph.getX() + (float) i * 3.8f, glyph.getCentreY() - h / 2.0f, 2.2f, h, 1.1f);
        }
        header.removeFromLeft (10);
        g.setColour (theme::textPrimary);
        g.setFont (theme::font (20.0f, juce::Font::bold));
        g.drawText ("BeatForge", header, juce::Justification::centredLeft);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (13.0f));
        g.drawText ("v" + toJuce (versionString()), header, juce::Justification::centredRight);

        // Master output panel.
        g.setColour (theme::panel);
        g.fillRoundedRectangle (outputCard.toFloat(), theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (outputCard.toFloat().reduced (0.5f), theme::cornerRadius, 1.0f);
        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont());
        g.drawText ("MASTER", outputCard.withHeight (34), juce::Justification::centred);
    }

    void MainComponent::resized()
    {
        auto area = getLocalBounds();
        headerArea = area.removeFromTop (48);
        transportBar.setBounds (area.removeFromTop (64));
        statusBar.setBounds (area.removeFromBottom (26));

        // Master output card, docked right like a mixer's master strip.
        auto body = area.reduced (16);
        outputCard = body.removeFromRight (120);
        body.removeFromRight (16);
        workspaceArea = body;

        // Editor below: at least the step sequencer's height, up to half the body.
        auto editor = body.removeFromBottom (juce::jmax (DrumMachinePanel::preferredHeight + 42, body.getHeight() / 2));
        body.removeFromBottom (10);
        arrangement.setBounds (body);

        workspaceTabs.setBounds (editor.removeFromTop (34).withWidth (workspaceTabs.getPreferredWidth()));
        editor.removeFromTop (8);
        pianoRoll.setBounds (editor);
        instrumentPanel.setBounds (editor);
        drumPanel.setBounds (editor.removeFromTop (DrumMachinePanel::preferredHeight));
        outputMeter.setBounds (outputCard.reduced (16, 0).withTrimmedTop (36).withTrimmedBottom (16));
    }
}
