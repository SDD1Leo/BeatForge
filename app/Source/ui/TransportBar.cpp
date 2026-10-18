#include "ui/TransportBar.h"
#include "ui/Theme.h"

#include <beatforge/time/MusicalTime.h>

namespace beatforge
{
    namespace
    {
        void drawSegment (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& value,
                          const juce::String& caption, float valueSize)
        {
            area = area.reduced (4, 4);
            auto captionArea = area.removeFromBottom (12);
            g.setColour (theme::textPrimary);
            g.setFont (theme::numericFont (valueSize));
            g.drawText (value, area, juce::Justification::centred);
            g.setColour (theme::textSecondary);
            g.setFont (theme::captionFont (9.5f));
            g.drawText (caption, captionArea, juce::Justification::centred);
        }
    }

    TransportBar::TransportBar (audio::AudioEngine& e, project::Session& s)
        : engine (e), session (s),
          undoButton ("Undo", TransportButton::Icon::undo, theme::accent),
          redoButton ("Redo", TransportButton::Icon::redo, theme::accent),
          playButton ("Play", TransportButton::Icon::play, theme::accent),
          stopButton ("Stop", TransportButton::Icon::stop, theme::textPrimary),
          recordButton ("Record", TransportButton::Icon::record, theme::record),
          loopButton ("Loop", TransportButton::Icon::loop, theme::accent),
          metronomeButton ("Metronome", TransportButton::Icon::metronome, theme::accent),
          keysButton ("Computer keyboard", TransportButton::Icon::keyboard, theme::accent),
          settingsButton ("Audio Settings", TransportButton::Icon::settings, theme::accent)
    {
        playButton.setTooltip ("Play (Space)");
        stopButton.setTooltip ("Stop and return to where playback started (Space)");
        loopButton.setTooltip ("Loop the region marked on the ruler (L)");
        metronomeButton.setTooltip ("Metronome on/off (M)");
        settingsButton.setTooltip ("Audio settings: output device, sample rate, buffer size");

        projectButton.onClick = [this] { if (onProjectMenu) onProjectMenu (projectButton); };
        playButton.onClick = [this] { engine.play(); refreshPosition(); };
        stopButton.onClick = [this] { engine.setRecording (false); engine.stop(); refreshPosition(); };
        recordButton.onClick = [this] { if (onRecord) onRecord(); };
        undoButton.onClick = [this] { session.undo(); };
        redoButton.onClick = [this] { session.redo(); };
        keysButton.onClick = [this] { if (onComputerKeys) onComputerKeys(); };
        loopButton.onClick = [this] { toggleLoop(); };
        metronomeButton.onClick = [this] { toggleMetronome(); };
        settingsButton.onClick = [this] { if (onOpenAudioSettings) onOpenAudioSettings(); };

        tempoField.onTempoChange = [this] (double bpm) { session.setTempo (bpm); };
        tempoField.onEditFinished = [this] { if (onTextEditFinished) onTextEditFinished(); };
        signatureField.onChange = [this] (time::TimeSignature sig) { session.setTimeSignature (sig); };

        for (auto* c : std::initializer_list<juce::Component*> { &projectButton, &undoButton, &redoButton, &playButton, &stopButton, &recordButton, &loopButton,
                                                                  &tempoField, &signatureField, &metronomeButton, &keysButton,
                                                                  &settingsButton })
            addAndMakeVisible (c);

        listenerId = session.addListener ([this] (project::Session::Change change)
        {
            using Change = project::Session::Change;
            if (change == Change::replaced || change == Change::transport || change == Change::metadata)
                refreshFromSession();
            else
                refreshUndo();   // every edit renames the undo step
        });

        refreshFromSession();
        setDeviceAvailable (false);
        startTimerHz (60);
    }

    TransportBar::~TransportBar()
    {
        session.removeListener (listenerId);
    }

    void TransportBar::refreshFromSession()
    {
        const auto& p = session.getProject();
        projectButton.setProjectName (juce::String::fromUTF8 (p.name.c_str()));
        tempoField.setTempo (p.tempo, juce::dontSendNotification);
        signatureField.setSignature (p.timeSignature);
        loopButton.setLit (p.loopEnabled);
        refreshUndo();
        metronomeButton.setLit (p.metronomeEnabled);
        repaint (savedArea);
    }

    void TransportBar::setAutosave (bool currentIsAutosaved, juce::Time when)
    {
        if (currentIsAutosaved != autosaved || when != autosaveTime)
        {
            autosaved = currentIsAutosaved;
            autosaveTime = when;
            repaint (savedArea);
        }
    }

    juce::String TransportBar::getTooltip()
    {
        if (! savedArea.contains (getMouseXYRelative()))
            return {};
        const auto file = session.getFile() ? juce::String (session.getFile()->string()) : juce::String();
        if (! session.isDirty())
            return file.isEmpty() ? "New project, not saved yet (Ctrl+S)" : "All changes saved in " + file;
        auto text = juce::String ("Unsaved changes - Ctrl+S to save") + (file.isNotEmpty() ? " to " + file : juce::String());
        if (autosaveTime != juce::Time())
            text += autosaved ? "\nA safety copy was made at " + autosaveTime.formatted ("%H:%M:%S") + " (restored if BeatForge crashes)"
                              : "\nLast safety copy " + autosaveTime.formatted ("%H:%M:%S") + "; the next one follows within a minute";
        else
            text += "\nA safety copy is made every minute while there are unsaved changes";
        return text;
    }

    void TransportBar::refreshUndo()
    {
        const auto u = juce::String::fromUTF8 (session.undoLabel().c_str()), r = juce::String::fromUTF8 (session.redoLabel().c_str());
        undoButton.setEnabled (u.isNotEmpty());
        redoButton.setEnabled (r.isNotEmpty());
        undoButton.setTooltip (u.isNotEmpty() ? "Undo " + u + " (Ctrl+Z)" : "Nothing to undo");
        redoButton.setTooltip (r.isNotEmpty() ? "Redo " + r + " (Ctrl+Shift+Z)" : "Nothing to redo");
    }

    void TransportBar::setDeviceAvailable (bool available)
    {
        deviceAvailable = available;
        playButton.setEnabled (available);
        playButton.setTooltip (available ? "Play (Space)" : "No audio device - open Audio Settings");
        refreshPosition();
    }

    void TransportBar::setRecordAvailable (bool available, const juce::String& tooltip)
    {
        recordButton.setEnabled (available);
        recordButton.setTooltip (tooltip);
    }

    void TransportBar::setRecording (bool isRecording)
    {
        recordButton.setLit (isRecording);
    }

    void TransportBar::setComputerKeysLit (bool lit, const juce::String& tooltip)
    {
        keysButton.setLit (lit);
        keysButton.setTooltip (tooltip);
    }

    void TransportBar::togglePlayStop()
    {
        if (engine.isPlayRequested())
        {
            engine.setRecording (false);
            engine.stop();
        }
        else if (deviceAvailable)
            engine.play();

        refreshPosition();
    }

    void TransportBar::toggleLoop()
    {
        const auto& p = session.getProject();
        session.setLoop (! p.loopEnabled, p.loopStart, p.loopEnd);
    }

    void TransportBar::toggleMetronome()
    {
        session.setMetronomeEnabled (! session.getProject().metronomeEnabled);
    }

    void TransportBar::timerCallback() { refreshPosition(); }

    void TransportBar::refreshPosition()
    {
        const auto ticks = engine.getPositionTicks();
        const auto sig = engine.getTimeSignature();
        const auto bbt = time::toBarBeatTick ((time::Tick) ticks, sig);
        const bool running = engine.getState() != audio::TransportState::stopped;

        const auto rate = engine.getSampleRate();
        const auto seconds = rate > 0.0 ? (double) engine.getPositionSamples() / rate : 0.0;
        const auto minutes = (int) (seconds / 60.0);

        auto newBbt = juce::String (bbt.bar) + "." + juce::String (bbt.beat) + "." + juce::String (bbt.tick).paddedLeft ('0', 3);
        auto newTime = juce::String::formatted ("%02d:%06.3f", minutes, seconds - minutes * 60.0);
        const int newBeat = running ? bbt.beat : 0;

        playButton.setLit (engine.isPlayRequested());

        if (newBbt != barBeatText || newTime != timeText || newBeat != currentBeat)
        {
            barBeatText = newBbt;
            timeText = newTime;
            currentBeat = newBeat;
            repaint (positionArea.getUnion (timeArea).getUnion (beatArea));
        }
    }

    void TransportBar::paint (juce::Graphics& g)
    {
        g.setColour (theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // Saved state next to the project name.
        {
            const bool dirty = session.isDirty();
            const bool neverSaved = ! session.getFile().has_value();
            const auto colour = dirty ? theme::amber : (neverSaved ? theme::textMuted : theme::ok);
            const auto label = dirty ? (autosaved ? "Autosaved" : "Unsaved") : (neverSaved ? "Not saved" : "Saved");

            auto area = savedArea;
            g.setColour (colour);
            g.fillEllipse (area.removeFromLeft (8).withSizeKeepingCentre (7, 7).toFloat());
            area.removeFromLeft (6);
            g.setColour (dirty ? theme::textPrimary : theme::textSecondary);
            g.setFont (theme::font (12.5f));
            g.drawText (label, area, juce::Justification::centredLeft);
        }

        // Readout strip: one dark instrument display split into segments.
        g.setColour (theme::background);
        g.fillRoundedRectangle (stripArea.toFloat(), theme::controlRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (stripArea.toFloat().reduced (0.5f), theme::controlRadius, theme::borderWidth);
        for (auto x : { tempoField.getRight(), signatureField.getRight(), showTime ? positionArea.getRight() : -1 })
            if (x > 0)
                g.drawVerticalLine (x, (float) stripArea.getY() + 8.0f, (float) stripArea.getBottom() - 8.0f);

        drawSegment (g, positionArea, barBeatText, "BAR.BEAT.TICK", 20.0f);
        if (showTime)
            drawSegment (g, timeArea, timeText, "TIME", 17.0f);

        // Beat lights: one per beat in the bar; downbeat magenta, others cyan.
        const auto beats = engine.getTimeSignature().numerator;
        const float size = 10.0f, gap = 5.0f;
        const float totalWidth = beats * size + (beats - 1) * gap;
        auto x = (float) beatArea.getCentreX() - totalWidth / 2.0f;

        for (int b = 1; b <= beats; ++b, x += size + gap)
        {
            const auto dot = juce::Rectangle<float> (x, (float) beatArea.getCentreY() - size / 2.0f, size, size);
            const bool lit = b == currentBeat;
            g.setColour (lit ? (b == 1 ? theme::magenta : theme::cyan) : theme::control);
            g.fillEllipse (dot);
            g.setColour (lit ? juce::Colours::transparentBlack : theme::border);
            g.drawEllipse (dot, 1.0f);
        }
    }

    void TransportBar::resized()
    {
        showTime = getWidth() >= minimumWidthForTime;
        auto area = getLocalBounds().reduced (16, 9);

        projectButton.setBounds (area.removeFromLeft (184).reduced (0, 4));
        area.removeFromLeft (10);
        savedArea = area.removeFromLeft (84);
        area.removeFromLeft (6);
        undoButton.setBounds (area.removeFromLeft (36).reduced (0, 5));
        area.removeFromLeft (4);
        redoButton.setBounds (area.removeFromLeft (36).reduced (0, 5));
        area.removeFromLeft (16);

        for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton })
        {
            b->setBounds (area.removeFromLeft (40).reduced (0, 3));
            area.removeFromLeft (6);
        }
        area.removeFromLeft (10);

        stripArea = area.removeFromLeft (108 + 64 + 128 + (showTime ? 104 : 0));
        auto strip = stripArea;
        tempoField.setBounds (strip.removeFromLeft (108));
        signatureField.setBounds (strip.removeFromLeft (64));
        positionArea = strip.removeFromLeft (128);
        timeArea = showTime ? strip : juce::Rectangle<int>();
        area.removeFromLeft (12);

        beatArea = area.removeFromLeft (84);
        area.removeFromLeft (12);
        metronomeButton.setBounds (area.removeFromLeft (40).reduced (0, 3));
        area.removeFromLeft (8);
        keysButton.setBounds (area.removeFromLeft (40).reduced (0, 3));

        settingsButton.setBounds (area.removeFromRight (40).reduced (0, 3));
    }
}
