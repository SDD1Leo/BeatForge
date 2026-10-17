#include "ui/TransportBar.h"
#include "ui/Theme.h"

namespace beatforge
{
    TransportBar::TransportBar (audio::AudioEngine& e)
        : engine (e),
          playButton ("Play", TransportButton::Icon::play, theme::ok),
          stopButton ("Stop", TransportButton::Icon::stop, theme::textPrimary)
    {
        playButton.setTooltip ("Play test tone (Space)");
        stopButton.setTooltip ("Stop (Space)");
        audioSettingsButton.setTooltip ("Choose the audio output device, sample rate and buffer size");
        audioSettingsButton.setWantsKeyboardFocus (false);

        playButton.onClick = [this] { engine.play(); refresh(); };
        stopButton.onClick = [this] { engine.stop(); refresh(); };
        audioSettingsButton.onClick = [this] { if (onOpenAudioSettings) onOpenAudioSettings(); };

        addAndMakeVisible (playButton);
        addAndMakeVisible (stopButton);
        addAndMakeVisible (audioSettingsButton);

        stateColour = theme::textMuted;
        setDeviceAvailable (false);
        startTimerHz (30);
    }

    void TransportBar::setDeviceAvailable (bool available)
    {
        deviceAvailable = available;
        playButton.setEnabled (available);
        playButton.setTooltip (available ? "Play test tone (Space)" : "No audio device - open Audio Settings");
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

    void TransportBar::timerCallback() { refresh(); }

    void TransportBar::refresh()
    {
        const auto state = engine.getState();
        const bool requested = engine.isPlayRequested();
        const auto rate = engine.getSampleRate();
        const auto seconds = rate > 0.0 ? (double) engine.getPositionSamples() / rate : 0.0;

        const auto minutes = (int) (seconds / 60.0);
        auto newTime = juce::String::formatted ("%02d:%06.3f", minutes, seconds - minutes * 60.0);

        juce::String newState;
        juce::Colour newColour;

        if (requested && state == audio::TransportState::playing) { newState = "PLAYING";  newColour = theme::ok; }
        else if (requested)                                       { newState = "STARTING"; newColour = theme::accent; }
        else if (state == audio::TransportState::stopping)        { newState = "STOPPING"; newColour = theme::textMuted; }
        else                                                      { newState = "STOPPED";  newColour = theme::textMuted; }

        playButton.setLit (requested);

        if (newTime != timeText || newState != stateText)
        {
            timeText = newTime;
            stateText = newState;
            stateColour = newColour;
            repaint();
        }
    }

    void TransportBar::paint (juce::Graphics& g)
    {
        g.setColour (theme::panel);
        g.fillRect (getLocalBounds());
        g.setColour (theme::border);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        // Time display.
        g.setColour (theme::background);
        g.fillRoundedRectangle (timeArea.toFloat(), theme::cornerRadius);
        g.setColour (theme::border);
        g.drawRoundedRectangle (timeArea.toFloat().reduced (0.5f), theme::cornerRadius, 1.0f);
        g.setColour (theme::textPrimary);
        g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::plain));
        g.drawText (timeText, timeArea, juce::Justification::centred);

        // State pill.
        auto pill = stateArea.toFloat().withSizeKeepingCentre ((float) stateArea.getWidth(), 22.0f);
        g.setColour (stateColour.withAlpha (0.15f));
        g.fillRoundedRectangle (pill, 11.0f);
        g.setColour (stateColour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (stateText, pill, juce::Justification::centred);

        // Source label: the only sound source in Phase 1.
        g.setColour (theme::textMuted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("Source: test tone  " + juce::String::fromUTF8 ("\xc2\xb7") + "  A4 440 Hz  "
                        + juce::String::fromUTF8 ("\xc2\xb7") + "  " + juce::String::fromUTF8 ("\xe2\x88\x92") + "18 dBFS",
                    toneArea, juce::Justification::centredLeft);
    }

    void TransportBar::resized()
    {
        auto area = getLocalBounds().reduced (16, 10);

        playButton.setBounds (area.removeFromLeft (40));
        area.removeFromLeft (6);
        stopButton.setBounds (area.removeFromLeft (40));
        area.removeFromLeft (16);
        timeArea = area.removeFromLeft (130);
        area.removeFromLeft (12);
        stateArea = area.removeFromLeft (86);

        audioSettingsButton.setBounds (area.removeFromRight (140));
        area.removeFromRight (16);
        area.removeFromLeft (20);
        toneArea = area;
    }
}
