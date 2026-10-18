#include "ui/TransportBar.h"
#include "ui/Theme.h"

#include <beatforge/BuildInfo.h>
#include <beatforge/time/MusicalTime.h>

namespace beatforge
{
    namespace
    {
        // A readout cell: Mono Medium 20 value over a 9 px caption.
        void drawSegment (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& value,
                          const juce::String& caption, juce::Colour valueColour)
        {
            area = area.reduced (2, 4);
            auto captionArea = area.removeFromBottom (12);
            g.setColour (valueColour);
            g.setFont (theme::numericFont (20.0f, juce::Font::bold));
            g.drawText (value, area, juce::Justification::centred);
            g.setColour (theme::textMuted);
            g.setFont (theme::captionFont (9.0f));
            g.drawText (caption, captionArea, juce::Justification::centred);
        }
    }

    TransportBar::TransportBar (audio::AudioEngine& e, project::Session& s)
        : engine (e), session (s),
          menuButton ("Project menu", TransportButton::Icon::menu, theme::accent),
          helpButton ("Help", TransportButton::Icon::help, theme::accent),
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
        playButton.setStyle (TransportButton::Style::primary);
        for (auto* b : { &stopButton, &recordButton, &loopButton })
            b->setStyle (TransportButton::Style::ghost);
        stopButton.setIconColour (theme::textPrimary);
        recordButton.setIconColour (theme::record);

        playButton.setTooltip ("Play (Space)");
        stopButton.setTooltip ("Stop and return to where playback started (Space)");
        loopButton.setTooltip ("Loop the region marked on the ruler (L)");
        metronomeButton.setTooltip ("Metronome on/off (M)");
        settingsButton.setTooltip ("Audio settings: output device, sample rate, buffer size");

        menuButton.setTooltip ("Project menu: New, Open, Save, Save As, Rename, Undo/Redo, Export Audio/MIDI");
        menuButton.onClick = [this] { if (onProjectMenu) onProjectMenu (menuButton); };
        helpButton.setTooltip ("Help: User Guide, Keyboard Shortcuts (F1)");
        helpButton.onClick = [this] { if (onHelpMenu) onHelpMenu (helpButton); };
        projectName.onRename = [this] (const juce::String& name) { return session.renameProject (name.toStdString()); };
        projectName.onEditFinished = [this] { if (onTextEditFinished) onTextEditFinished(); };
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

        for (auto* c : std::initializer_list<juce::Component*> { &menuButton, &projectName, &helpButton, &undoButton, &redoButton, &playButton, &stopButton, &recordButton, &loopButton,
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
        projectName.setProjectName (juce::String::fromUTF8 (p.name.c_str()));
        tempoField.setTempo (p.tempo, juce::dontSendNotification);
        signatureField.setSignature (p.timeSignature);
        loopButton.setLit (p.loopEnabled);
        refreshUndo();
        metronomeButton.setLit (p.metronomeEnabled);
        resized();   // the beat lights follow the time signature
        repaint();
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
        g.setColour (theme::bgPanel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::borderSubtle);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // Logo: six bars alternating purple and pink, then the wordmark.
        {
            auto logo = brandArea.removeFromLeft (28).withSizeKeepingCentre (28, 26).toFloat();
            const float heights[] { 8.0f, 16.0f, 26.0f, 14.0f, 22.0f, 10.0f };
            for (int i = 0; i < 6; ++i)
            {
                g.setColour (i % 2 == 0 ? theme::purple : theme::pink);
                g.fillRoundedRectangle (logo.getX() + 0.5f + (float) i * 5.0f, logo.getCentreY() - heights[i] / 2.0f, 3.0f, heights[i], 1.5f);
            }
            if (showWordmark)
            {
                brandArea.removeFromLeft (10);
                g.setColour (theme::textPrimary);
                g.setFont (theme::heavyFont (18.0f));
                g.drawText ("BeatForge", brandArea, juce::Justification::centredLeft, false);
            }
        }

        // Project name card (the menu button beside it is its own card).
        g.setColour (theme::bgSurface);
        g.fillRoundedRectangle (projectArea.toFloat(), 8.0f);
        g.setColour (projectName.isMouseOver (true) ? theme::borderStrong : theme::border);
        g.drawRoundedRectangle (projectArea.toFloat().reduced (0.5f), 8.0f, theme::borderWidth);

        // Save-state pill.
        {
            const bool dirty = session.isDirty();
            const bool neverSaved = ! session.getFile().has_value();
            const auto colour = dirty ? theme::amber : (neverSaved ? theme::textMuted : theme::ok);
            const auto label = dirty ? (autosaved ? "Autosaved" : "Unsaved") : (neverSaved ? "Not saved" : "Saved");

            g.setFont (theme::font (11.0f));
            const auto textWidth = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label));
            auto pill = savedArea.withWidth (juce::jmin (savedArea.getWidth(), 10 + 6 + 6 + textWidth + 10));
            g.setColour (colour.withAlpha (0.12f));
            g.fillRoundedRectangle (pill.toFloat(), (float) pill.getHeight() / 2.0f);
            auto inner = pill.reduced (10, 0);
            g.setColour (colour);
            g.fillEllipse (inner.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
            inner.removeFromLeft (6);
            g.setColour (theme::textSecondary);
            g.drawText (label, inner, juce::Justification::centredLeft, false);
        }

        // Transport group.
        g.setColour (theme::bgSurface);
        g.fillRoundedRectangle (groupArea.toFloat(), 12.0f);
        g.setColour (theme::borderSubtle);
        g.drawRoundedRectangle (groupArea.toFloat().reduced (0.5f), 12.0f, theme::borderWidth);

        // Time display: one inset well split into cells.
        g.setColour (theme::bgInset);
        g.fillRoundedRectangle (stripArea.toFloat(), 12.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (stripArea.toFloat().reduced (0.5f), 12.0f, theme::borderWidth);
        for (auto x : { tempoField.getRight(), signatureField.getRight(), showTime ? positionArea.getRight() : -1 })
            if (x > 0)
                g.fillRect (juce::Rectangle<int> (x, stripArea.getCentreY() - 15, 1, 30));

        drawSegment (g, positionArea, barBeatText, "BAR.BEAT.TICK", theme::cyan);
        if (showTime)
            drawSegment (g, timeArea, timeText, "TIME", theme::textPrimary);

        // Beat dots: the current beat in purple, with a BEAT caption.
        const auto beats = engine.getTimeSignature().numerator;
        const float size = 9.0f, gap = 5.0f;
        const float totalWidth = (float) beats * size + (float) (beats - 1) * gap;
        auto x = (float) beatArea.getCentreX() - totalWidth / 2.0f;
        const float y = (float) beatArea.getY() + 6.0f;
        for (int b = 1; b <= beats; ++b, x += size + gap)
        {
            const auto dot = juce::Rectangle<float> (x, y, size, size);
            const bool lit = b == currentBeat;
            g.setColour (lit ? theme::purple : theme::bgControl);
            g.fillEllipse (dot);
            if (! lit)
            {
                g.setColour (theme::border);
                g.drawEllipse (dot.reduced (0.5f), 1.0f);
            }
        }
        g.setColour (theme::textMuted);
        g.setFont (theme::captionFont (8.0f));
        g.drawText ("BEAT", beatArea.withTrimmedTop (20), juce::Justification::centredTop, false);

        if (showVersion)
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::numericFont (11.0f));
            g.drawText ("v" + juce::String (versionString().data(), versionString().size()), versionArea, juce::Justification::centredRight, false);
        }
    }

    void TransportBar::resized()
    {
        // Spec section 4: brand and project on the left, the transport group,
        // readouts and toggles centred, version and settings on the right.
        // Narrow windows drop the version, the TIME cell, the wordmark and some
        // project-name width, in that order.
        auto area = getLocalBounds().withTrimmedLeft (20).withTrimmedRight (16);
        const int width = area.getWidth();

        constexpr int logoWidth = 28, wordmarkWidth = 104, gap = 12;
        constexpr int groupWidth = 202, cells = 92 + 64 + 124, timeCell = 124, readoutPad = 16;
        constexpr int toggles = 40 * 3 + 8 * 2;
        const int beatWidth = juce::jmax (44, engine.getTimeSignature().numerator * 14);
        const int savedWidth = 92, undoWidth = 36 * 2 + 8;
        int nameWidth = 170;

        auto centreWidth = [&] { return groupWidth + gap + cells + (showTime ? timeCell : 0) + readoutPad + gap + beatWidth + gap + toggles; };
        auto leftWidth = [&] { return logoWidth + (showWordmark ? 10 + wordmarkWidth : 0) + gap + 38 + 6 + nameWidth + gap + savedWidth + gap + undoWidth; };
        auto rightWidth = [&] { return 40 + (showVersion ? gap + 64 : 0); };
        auto fits = [&] { return leftWidth() + gap + centreWidth() + gap + rightWidth() <= width; };

        showVersion = showTime = showWordmark = true;
        if (! fits()) showVersion = false;
        if (! fits()) showTime = false;
        if (! fits()) showWordmark = false;
        while (! fits() && nameWidth > 80) nameWidth -= 10;

        auto centred = [] (juce::Rectangle<int> r, int h) { return r.withSizeKeepingCentre (r.getWidth(), h); };

        // Left block.
        auto left = area;
        brandArea = left.removeFromLeft (logoWidth + (showWordmark ? 10 + wordmarkWidth : 0));
        left.removeFromLeft (gap);
        menuButton.setBounds (centred (left.removeFromLeft (38), 38));
        left.removeFromLeft (6);
        projectArea = centred (left.removeFromLeft (nameWidth), 38);
        projectName.setBounds (projectArea.reduced (8, 1));
        left.removeFromLeft (gap);
        savedArea = centred (left.removeFromLeft (savedWidth), 26);
        left.removeFromLeft (gap);
        undoButton.setBounds (centred (left.removeFromLeft (36), 36));
        left.removeFromLeft (8);
        redoButton.setBounds (centred (left.removeFromLeft (36), 36));
        const int leftEnd = left.getX();

        // Right block.
        auto right = area;
        settingsButton.setBounds (centred (right.removeFromRight (40), 40));
        right.removeFromRight (gap);
        versionArea = showVersion ? right.removeFromRight (64) : juce::Rectangle<int>();
        const int rightStart = right.getRight() - (showVersion ? 0 : gap);

        // Centre block: centred on the window when there is room, else packed.
        const int w = centreWidth();
        const int x = juce::jlimit (leftEnd + gap, juce::jmax (leftEnd + gap, rightStart - gap - w), (getWidth() - w) / 2);
        auto centre = juce::Rectangle<int> (x, 0, w, getHeight());

        groupArea = centred (centre.removeFromLeft (groupWidth), 50);
        {
            auto g = groupArea.reduced (4);
            for (auto* b : { &stopButton, &playButton, &recordButton, &loopButton })
            {
                b->setBounds (g.removeFromLeft (b == &playButton ? 54 : 42));
                g.removeFromLeft (4);
            }
        }
        centre.removeFromLeft (gap);

        stripArea = centred (centre.removeFromLeft (cells + (showTime ? timeCell : 0) + readoutPad), 54);
        auto strip = stripArea.reduced (8, 3);
        tempoField.setBounds (strip.removeFromLeft (92));
        signatureField.setBounds (strip.removeFromLeft (64));
        positionArea = strip.removeFromLeft (124);
        timeArea = showTime ? strip : juce::Rectangle<int>();
        centre.removeFromLeft (gap);

        beatArea = centred (centre.removeFromLeft (beatWidth), 36);
        centre.removeFromLeft (gap);
        metronomeButton.setBounds (centred (centre.removeFromLeft (40), 40));
        centre.removeFromLeft (8);
        keysButton.setBounds (centred (centre.removeFromLeft (40), 40));
        centre.removeFromLeft (8);
        helpButton.setBounds (centred (centre.removeFromLeft (40), 40));
    }
}
