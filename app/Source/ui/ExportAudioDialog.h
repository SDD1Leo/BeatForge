#pragma once

#include <beatforge/project/AudioExport.h>
#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace beatforge
{
    // NEBULA "Export Audio" dialog: range, sample rate and bit depth, then a
    // file, then the render on a background thread with progress and Cancel,
    // then a summary. Renders a snapshot of the project, so editing and live
    // playback carry on meanwhile. Choices are remembered in the settings.
    class ExportAudioDialog final : public juce::Component,
                                    private juce::Timer
    {
    public:
        ExportAudioDialog (const project::Session&, juce::PropertiesFile& settings, juce::File defaultFolder, double deviceRate);
        ~ExportAudioDialog() override;

        std::function<void()> onClose;

        void paint (juce::Graphics&) override;
        void resized() override;

        // "song.wav: 0:09.4 (8.0 s + 1.4 s tail), 48 kHz 24-bit, peak -3.2 dBFS" (+ warnings).
        static juce::String describe (const project::AudioExportResult&, const project::AudioExportOptions&, const juce::File&);
        static project::AudioExportOptions optionsFrom (juce::PropertiesFile&);

    private:
        enum class State { choosing, rendering, done };

        void timerCallback() override;
        void chooseFileAndExport();
        void start (const juce::File&);
        void finish();
        void setState (State);
        project::AudioExportOptions currentOptions() const;
        void saveChoices();

        const project::Session& session;
        juce::PropertiesFile& settings;
        juce::File folder;

        juce::ComboBox rangeBox, rateBox, bitsBox;
        juce::TextButton exportButton { "Export..." }, cancelButton { "Cancel" };
        std::unique_ptr<juce::FileChooser> chooser;

        State state = State::choosing;
        juce::File target;
        project::AudioExportOptions running;
        project::AudioExportProgress progress;
        project::AudioExportResult result;
        std::atomic<bool> finished { false };
        std::thread worker;
        juce::String message;
        bool messageIsError = false;
        juce::Rectangle<int> optionsArea, progressArea, messageArea;
    };
}
