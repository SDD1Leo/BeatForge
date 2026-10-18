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
        : devices (d), engine (e), session (s), appSettings (settings), actions (s, settings, *this),
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
        selectionListenerId = selection.addListener ([this]
        {
            showTab (workspaceTabs.getCurrentTab());
            updateLiveInput();
        });
        sessionListenerId = session.addListener ([this] (project::Session::Change change)
        {
            if (change != project::Session::Change::metadata && change != project::Session::Change::transport)
                updateLiveInput();
        });
        transportBar.onRecord = [this] { toggleRecord(); };
        actions.onExportAudio = [this] { openExportAudio(); };
        transportBar.onComputerKeys = [this] { toggleComputerKeys(); };
        pianoRoll.keyFilter = [this] (const juce::KeyPress& key) { return computerKeys.keyPressed (key); };
        computerKeys.onChange = [this] { refreshComputerKeys(); };
        computerKeys.setEnabled (appSettings.getBoolValue ("computerKeyboard", false));
        refreshComputerKeys();

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
        startTimerHz (30);   // recorded notes, MIDI light; device status every 0.5 s
        showTab (0);
        updateLiveInput();

        setSize (1280, 800);
    }

    MainComponent::~MainComponent()
    {
        aiBridge = nullptr;   // first: its requests use the session and the selection
        selection.removeListener (selectionListenerId);
        session.removeListener (sessionListenerId);
        devices.removeChangeListener (this);
        engine.setLiveTarget (-1);

        if (settingsWindow != nullptr)
            delete settingsWindow.getComponent();
        if (exportWindow != nullptr)
            delete exportWindow.getComponent();   // cancels a running export
    }

    void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*) { refreshDeviceStatus(); }
    void MainComponent::timerCallback()
    {
        pollRecording();
        computerKeys.keyStateChanged();   // a key released while another window had focus

        // MIDI light: on for ~150 ms after any message.
        if (const auto count = devices.getMidiEventCount(); count != lastMidiCount)
        {
            lastMidiCount = count;
            midiLightTicks = 5;
        }
        else if (midiLightTicks > 0)
        {
            --midiLightTicks;
        }
        statusBar.setMidi (midiInputsText + computerKeysText + (aiBridge != nullptr ? dot + aiBridge->statusText() : juce::String()),
                           midiLightTicks > 0);

        if (++slowTicks >= 15)
        {
            slowTicks = 0;
            refreshDeviceStatus();     // the dropout counter has no change notification
            engine.collectGarbage();   // free schedules the audio thread has finished with
        }
    }

    void MainComponent::updateLiveInput()
    {
        const auto* selected = session.findTrack (selection.getTrack());
        const bool synth = selected != nullptr && selected->type == project::TrackType::instrument;

        // While a take runs, the keys keep playing the track being recorded.
        const auto target = recorder.isActive() ? recorder.getTrack() : synth ? selected->id : 0;
        engine.setLiveTarget (target != 0 ? session.slotOf (target) : -1);

        if (recorder.isActive())
            transportBar.setRecordAvailable (true, "Recording - press again (R) to stop recording, Stop to end");
        else if (! deviceOpen)
            transportBar.setRecordAvailable (false, "No audio device - open Audio Settings");
        else if (synth)
            transportBar.setRecordAvailable (true, "Record into " + juce::String::fromUTF8 (selected->name.c_str())
                                                       + " (R): play your MIDI keyboard or the piano roll keys");
        else
            transportBar.setRecordAvailable (false, "Select a synth track to record into it");
    }

    bool MainComponent::startAiBridge (std::string& error)
    {
        api::CommandApi::Environment env;
        env.allowedFolders = { actions.defaultFolder().getFullPathName().toStdString() };
        env.appVersion = ProjectActions::appVersion().toStdString();
        env.audioAvailable = [this] { return deviceOpen; };
        env.reveal = [this] (project::TrackId track, project::ClipId clip)
        {
            if (clip != 0) selection.selectClip (clip);
            else           selection.selectTrack (track);
        };
        env.fileSaved = [this] (const std::filesystem::path& file) { actions.noteSaved (juce::File (juce::String (file.string()))); };

        aiBridge = std::make_unique<AiBridge> (session, engine, std::move (env));
        return aiBridge->start (error);
    }

    void MainComponent::toggleComputerKeys()
    {
        computerKeys.setEnabled (! computerKeys.isEnabled());
        appSettings.setValue ("computerKeyboard", computerKeys.isEnabled());
        appSettings.saveIfNeeded();
    }

    void MainComponent::refreshComputerKeys()
    {
        auto name = [] (int pitch) { return juce::MidiMessage::getMidiNoteName (pitch, true, true, 4); };
        const auto on = computerKeys.isEnabled();
        computerKeysText = on ? dot + "Keys " + name (computerKeys.getLowestNote()) + "-" + name (computerKeys.getHighestNote())
                                    + ", velocity " + juce::String (computerKeys.getVelocity())
                              : juce::String();
        transportBar.setComputerKeysLit (on, on ? "Computer keyboard ON (K): Q-] play notes, 2-= sharps, Z/X octave, C/V velocity. "
                                                  "Record with Ctrl+R or the Record button"
                                                : "Play notes with the computer keyboard (K)");
    }

    void MainComponent::toggleRecord()
    {
        if (engine.isRecording())
        {
            engine.setRecording (false);   // punch out, keep playing
            return;
        }

        const auto* t = session.findTrack (selection.getTrack());
        if (t == nullptr || t->type != project::TrackType::instrument || ! deviceOpen || ! recorder.begin (t->id))
            return;

        updateLiveInput();
        engine.setRecording (true);
        if (! engine.isPlayRequested())
            transportBar.togglePlayStop();
        transportBar.setRecording (true);
    }

    void MainComponent::pollRecording()
    {
        audio::AudioEngine::RecordedEvent e;
        while (engine.popRecorded (e))
            recorder.add (e);

        // Recording switched off before the engine started a take: nothing will follow.
        if (recorder.isActive() && ! engine.isRecording() && engine.getState() == audio::TransportState::stopped)
            recorder.finish();

        const bool recording = engine.isRecording() || recorder.isActive();
        transportBar.setRecording (recording);

        if (takeWasActive && ! recorder.isActive())
        {
            if (recorder.getLastClip() != 0)
            {
                selection.selectClip (recorder.getLastClip());   // show the take in the piano roll
                pianoRoll.scrollToNotes();
            }
            updateLiveInput();
        }
        takeWasActive = recorder.isActive();
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
        midiInputsText = s.midiInputs.isEmpty() ? juce::String ("No MIDI input") : "MIDI in: " + s.midiInputs.joinIntoString (", ");
        if (deviceOpen != s.deviceOpen)
        {
            deviceOpen = s.deviceOpen;
            updateLiveInput();
        }

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
            devices.getDeviceManager(), 0, 0, 1, 2, true, false, true, false);
        selector->setSize (560, 480);

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

    void MainComponent::openExportAudio()
    {
        if (exportWindow != nullptr)
        {
            exportWindow->toFront (true);
            return;
        }

        auto folder = juce::File (appSettings.getValue ("exportFolder"));
        if (session.getFile())
            folder = juce::File (juce::String (session.getFile()->parent_path().string()));
        else if (! folder.isDirectory())
            folder = actions.defaultFolder();

        const auto status = devices.getStatus();
        auto dialog = std::make_unique<ExportAudioDialog> (session, appSettings, folder, status.sampleRate);
        dialog->onClose = [this]
        {
            // Not from inside the button's own click handler.
            juce::MessageManager::callAsync ([safe = exportWindow] { if (safe != nullptr) delete safe.getComponent(); });
            grabKeyboardFocus();
        };

        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Export Audio";
        options.dialogBackgroundColour = theme::panel;
        options.content.setOwned (dialog.release());
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.escapeKeyTriggersCloseButton = true;
        exportWindow = options.launchAsync();
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        return handleShortcut (key);
    }

    bool MainComponent::keyStateChanged (bool)
    {
        computerKeys.keyStateChanged();
        return false;
    }

    bool MainComponent::handleShortcut (const juce::KeyPress& key)
    {
        const auto ctrl = juce::ModifierKeys::commandModifier;

        // Computer keyboard notes win over single-key shortcuts while it is on.
        if (computerKeys.keyPressed (key))
            return true;
        if (key == juce::KeyPress ('k') || key == juce::KeyPress ('K')) { toggleComputerKeys(); return true; }
        if (key == juce::KeyPress ('r', ctrl, 0))  { toggleRecord(); return true; }
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
        if (key == juce::KeyPress ('e', ctrl, 0))  { actions.exportMidiWithChooser(); return true; }
        if (key == juce::KeyPress ('e', ctrl | juce::ModifierKeys::shiftModifier, 0)
            || key == juce::KeyPress ('E', ctrl | juce::ModifierKeys::shiftModifier, 0)) { openExportAudio(); return true; }
        if (key == juce::KeyPress ('=', ctrl, 0) || key == juce::KeyPress ('+', ctrl, 0)
            || key == juce::KeyPress ('+', ctrl | juce::ModifierKeys::shiftModifier, 0)) { arrangement.zoomIn(); return true; }
        if (key == juce::KeyPress ('-', ctrl, 0))  { arrangement.zoomOut(); return true; }

        if (key == juce::KeyPress ('1', ctrl, 0))  { showTab (0); return true; }
        if (key == juce::KeyPress ('2', ctrl, 0))  { showTab (1); return true; }

        if (key == juce::KeyPress::spaceKey)      { transportBar.togglePlayStop();  return true; }
        // Single-letter shortcuts only without modifiers (Ctrl+L etc. stay free).
        if (key == juce::KeyPress ('m') || key == juce::KeyPress ('M')) { transportBar.toggleMetronome(); return true; }
        if (key == juce::KeyPress ('l') || key == juce::KeyPress ('L')) { transportBar.toggleLoop();      return true; }
        if (key == juce::KeyPress ('r') || key == juce::KeyPress ('R')) { toggleRecord();                 return true; }

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
