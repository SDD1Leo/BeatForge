#include "ui/TransportBar.h"
#include "ui/Theme.h"

#include <beatforge/time/MusicalTime.h>

namespace beatforge
{
    namespace
    {
        struct SignatureChoice { const char* name; time::TimeSignature sig; };
        const SignatureChoice signatures[] {
            { "2/4", { 2, 4 } }, { "3/4", { 3, 4 } }, { "4/4", { 4, 4 } },
            { "5/4", { 5, 4 } }, { "6/8", { 6, 8 } }, { "7/8", { 7, 8 } }
        };
        const int loopLengths[] { 1, 2, 4, 8 };
    }

    TransportBar::TransportBar (audio::AudioEngine& e)
        : engine (e),
          playButton ("Play", TransportButton::Icon::play, theme::accent),
          stopButton ("Stop", TransportButton::Icon::stop, theme::textPrimary),
          loopButton ("Loop", TransportButton::Icon::loop, theme::accent),
          metronomeButton ("Metronome", TransportButton::Icon::metronome, theme::accent)
    {
        playButton.setTooltip ("Play (Space)");
        stopButton.setTooltip ("Stop and return to bar 1 (Space)");
        loopButton.setTooltip ("Loop from bar 1 (L)");
        metronomeButton.setTooltip ("Metronome on/off (M)");
        audioSettingsButton.setTooltip ("Choose the audio output device, sample rate and buffer size");

        playButton.onClick = [this] { engine.play(); refresh(); };
        stopButton.onClick = [this] { engine.stop(); refresh(); };
        loopButton.onClick = [this] { toggleLoop(); };
        metronomeButton.onClick = [this] { toggleMetronome(); };
        audioSettingsButton.onClick = [this] { if (onOpenAudioSettings) onOpenAudioSettings(); };

        tempoField.setTempo (engine.getTempo(), juce::dontSendNotification);
        tempoField.onTempoChange = [this] (double bpm) { engine.setTempo (bpm); };
        tempoField.onEditFinished = [this] { if (onTextEditFinished) onTextEditFinished(); };

        for (int i = 0; i < (int) std::size (signatures); ++i)
            signatureBox.addItem (signatures[i].name, i + 1);
        signatureBox.setSelectedId (3, juce::dontSendNotification);   // 4/4
        signatureBox.setTooltip ("Time signature");
        signatureBox.onChange = [this]
        {
            engine.setTimeSignature (signatures[signatureBox.getSelectedId() - 1].sig);
            applyLoop();   // loop length is in bars, so its tick length changes
        };

        for (auto bars : loopLengths)
            loopLengthBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars);
        loopLengthBox.setSelectedId (4, juce::dontSendNotification);
        loopLengthBox.setTooltip ("Loop length, from bar 1");
        loopLengthBox.onChange = [this] { applyLoop(); };

        for (auto* c : std::initializer_list<juce::Component*> { &signatureBox, &loopLengthBox, &audioSettingsButton })
            c->setWantsKeyboardFocus (false);   // keep Space for the transport

        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &loopButton, &metronomeButton,
                                                                  &tempoField, &signatureBox, &loopLengthBox, &audioSettingsButton })
            addAndMakeVisible (c);

        setDeviceAvailable (false);
        startTimerHz (60);
    }

    void TransportBar::applyLoop()
    {
        const auto bars = loopLengthBox.getSelectedId();
        const auto barTicks = engine.getTimeSignature().ticksPerBar();
        engine.setLoop (loopOn, 0, barTicks * bars);
        loopButton.setLit (loopOn);

        if (onLoopChanged)
            onLoopChanged (loopOn, bars);
    }

    void TransportBar::setDeviceAvailable (bool available)
    {
        deviceAvailable = available;
        playButton.setEnabled (available);
        playButton.setTooltip (available ? "Play (Space)" : "No audio device - open Audio Settings");
        refresh();
    }

    void TransportBar::togglePlayStop()
    {
        if (engine.isPlayRequested())
            engine.stop();
        else if (deviceAvailable)
            engine.play();

        refresh();
    }

    void TransportBar::toggleLoop()
    {
        loopOn = ! loopOn;
        applyLoop();
    }

    void TransportBar::toggleMetronome()
    {
        engine.setMetronomeEnabled (! engine.isMetronomeEnabled());
        refresh();
    }

    void TransportBar::timerCallback() { refresh(); }

    void TransportBar::refresh()
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
        metronomeButton.setLit (engine.isMetronomeEnabled());

        if (newBbt != barBeatText || newTime != timeText || newBeat != currentBeat)
        {
            barBeatText = newBbt;
            timeText = newTime;
            currentBeat = newBeat;
            repaint (positionArea.getUnion (beatArea));
        }
    }

    void TransportBar::paint (juce::Graphics& g)
    {
        g.setColour (theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // Position display: bars.beats.ticks large, clock time small.
        g.setColour (theme::background);
        g.fillRoundedRectangle (positionArea.toFloat(), theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (positionArea.toFloat().reduced (0.5f), theme::cornerRadius, 1.0f);

        auto text = positionArea.reduced (12, 3);
        g.setColour (theme::textPrimary);
        g.setFont (theme::numericFont (20.0f));
        g.drawText (barBeatText, text.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (theme::textSecondary);
        g.setFont (theme::numericFont (11.0f));
        g.drawText (timeText, text, juce::Justification::centredLeft);

        // Beat lights: one per beat in the bar; downbeat in accent colour.
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
            g.setColour (theme::border);
            g.drawEllipse (dot, 1.0f);
        }
    }

    void TransportBar::resized()
    {
        auto area = getLocalBounds().reduced (16, 10);

        auto place = [&area] (juce::Component& c, int width, int gapAfter)
        {
            c.setBounds (area.removeFromLeft (width));
            area.removeFromLeft (gapAfter);
        };

        place (playButton, 40, 6);
        place (stopButton, 40, 6);
        place (loopButton, 40, 16);

        positionArea = area.removeFromLeft (128);
        area.removeFromLeft (8);
        beatArea = area.removeFromLeft (110);
        area.removeFromLeft (8);

        place (tempoField, 116, 8);
        signatureBox.setBounds (area.removeFromLeft (70).reduced (0, 4));
        area.removeFromLeft (8);
        place (metronomeButton, 40, 8);
        loopLengthBox.setBounds (area.removeFromLeft (84).reduced (0, 4));

        audioSettingsButton.setBounds (area.removeFromRight (130));
    }
}
