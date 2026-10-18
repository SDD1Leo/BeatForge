#include "ui/ExportAudioDialog.h"
#include "project/FilePaths.h"
#include "ui/Theme.h"

#include <cmath>

namespace beatforge
{
    namespace
    {
        using Range = project::AudioExportOptions::Range;
        using audio::SampleFormat;

        const double rates[] { 44100.0, 48000.0, 96000.0 };
        const SampleFormat formats[] { SampleFormat::int16, SampleFormat::int24, SampleFormat::float32 };

        juce::String formatName (SampleFormat f)
        {
            return f == SampleFormat::int16 ? "16-bit" : f == SampleFormat::int24 ? "24-bit" : "32-bit float";
        }

        juce::String rateName (double r) { return juce::String (r / 1000.0, std::fmod (r, 1000.0) > 1.0 ? 1 : 0) + " kHz"; }

        juce::String clock (double seconds)
        {
            const auto m = (int) (seconds / 60.0);
            return juce::String (m) + ":" + juce::String (seconds - m * 60.0, 1).paddedLeft ('0', 4);
        }
    }

    project::AudioExportOptions ExportAudioDialog::optionsFrom (juce::PropertiesFile& settings)
    {
        project::AudioExportOptions o;
        o.range = settings.getValue ("exportRange") == "loop" ? Range::loop : Range::song;
        o.sampleRate = settings.getDoubleValue ("exportRate", 48000.0);
        const auto bits = settings.getIntValue ("exportBits", 24);
        o.format = bits == 16 ? SampleFormat::int16 : bits == 32 ? SampleFormat::float32 : SampleFormat::int24;
        return o;
    }

    ExportAudioDialog::ExportAudioDialog (project::Session& s, juce::PropertiesFile& st, juce::File defaultFolder, double deviceRate)
        : session (s), settings (st), folder (std::move (defaultFolder))
    {
        refreshRanges();
        sessionListener = session.addListener ([this] (project::Session::Change) { refreshRanges(); });
        for (int i = 0; i < 3; ++i)
        {
            rateBox.addItem (rateName (rates[i]), i + 1);
            bitsBox.addItem (formatName (formats[i]), i + 1);
        }

        // Remembered choices; the first time, the device's rate if it is one of ours.
        auto o = optionsFrom (settings);
        if (! settings.containsKey ("exportRate"))
            for (double r : rates)
                if (std::abs (r - deviceRate) < 1.0) o.sampleRate = r;
        rangeBox.setSelectedId (o.range == Range::loop || project::songEnd (session.getProject()) <= 0 ? 2 : 1, juce::dontSendNotification);
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs (rates[i] - o.sampleRate) < 1.0) rateBox.setSelectedId (i + 1, juce::dontSendNotification);
            if (formats[i] == o.format) bitsBox.setSelectedId (i + 1, juce::dontSendNotification);
        }
        if (rateBox.getSelectedId() == 0) rateBox.setSelectedId (2, juce::dontSendNotification);

        rangeBox.setTooltip ("What to render");
        rateBox.setTooltip ("Sample rate of the file");
        bitsBox.setTooltip ("16-bit: CDs and sharing. 24-bit: mastering, most DAWs. 32-bit float: never clips");
        exportButton.setTooltip ("Choose where to save, then render");
        exportButton.setColour (juce::TextButton::buttonColourId, theme::purple);
        exportButton.onClick = [this]
        {
            if (state == State::done) { if (onClose) onClose(); return; }
            chooseFileAndExport();
        };
        cancelButton.onClick = [this]
        {
            if (state == State::rendering) progress.cancel = true;   // the worker stops at the next block
            else if (onClose) onClose();
        };

        for (auto* c : std::initializer_list<juce::Component*> { &rangeBox, &rateBox, &bitsBox, &exportButton, &cancelButton })
            addAndMakeVisible (c);

        setSize (560, 330);
    }

    void ExportAudioDialog::refreshRanges()
    {
        const auto& p = session.getProject();
        const auto perBar = p.timeSignature.ticksPerBar();
        const auto secondsPerTick = 60.0 / (p.tempo * 960.0);
        const auto end = project::songEnd (p);
        const auto selected = rangeBox.getSelectedId();

        rangeBox.clear (juce::dontSendNotification);
        rangeBox.addItem ("Whole song: bars 1-" + juce::String ((int) ((end + perBar - 1) / perBar))
                              + " (" + clock ((double) end * secondsPerTick) + ") + tails", 1);
        rangeBox.addItem ("Loop region: bars " + juce::String (p.loopStart / perBar + 1) + "-" + juce::String (p.loopEnd / perBar + 1)
                              + " (" + clock ((double) (p.loopEnd - p.loopStart) * secondsPerTick) + "), loops seamlessly", 2);
        rangeBox.setItemEnabled (1, end > 0);
        rangeBox.setSelectedId (end <= 0 ? 2 : (selected != 0 ? selected : 1), juce::dontSendNotification);
    }

    ExportAudioDialog::~ExportAudioDialog()
    {
        session.removeListener (sessionListener);
        progress.cancel = true;
        if (worker.joinable())
            worker.join();   // the render stops within one block
    }

    project::AudioExportOptions ExportAudioDialog::currentOptions() const
    {
        project::AudioExportOptions o;
        o.range = rangeBox.getSelectedId() == 2 ? Range::loop : Range::song;
        o.sampleRate = rates[juce::jlimit (0, 2, rateBox.getSelectedId() - 1)];
        o.format = formats[juce::jlimit (0, 2, bitsBox.getSelectedId() - 1)];
        return o;
    }

    void ExportAudioDialog::saveChoices()
    {
        const auto o = currentOptions();
        settings.setValue ("exportRange", o.range == Range::loop ? "loop" : "song");
        settings.setValue ("exportRate", o.sampleRate);
        settings.setValue ("exportBits", audio::bitsOf (o.format));
        settings.setValue ("exportFolder", folder.getFullPathName());
        settings.saveIfNeeded();
    }

    void ExportAudioDialog::chooseFileAndExport()
    {
        folder.createDirectory();
        const auto& p = session.getProject();
        const auto suggested = folder.getChildFile (juce::File::createLegalFileName (juce::String::fromUTF8 (p.name.c_str())))
                                   .withFileExtension (".wav");
        chooser = std::make_unique<juce::FileChooser> ("Export audio", suggested, "*.wav", false);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<ExportAudioDialog> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResult() == juce::File())
                return;
            auto file = fc.getResult();
            if (! file.hasFileExtension (".wav;.wave"))
                file = file.withFileExtension (".wav");
            safe->folder = file.getParentDirectory();
            safe->start (file);
        });
    }

    void ExportAudioDialog::start (const juce::File& file)
    {
        saveChoices();
        target = file;
        running = currentOptions();
        progress.fraction = 0.0f;
        progress.cancel = false;
        finished = false;
        setState (State::rendering);

        // A snapshot: later edits do not reach this render.
        worker = std::thread ([this, snapshot = session.getProject(), path = toPath (file)]
        {
            result = project::exportAudio (snapshot, path, running, &progress);
            finished = true;
        });
        startTimerHz (30);
    }

    void ExportAudioDialog::timerCallback()
    {
        repaint (progressArea.getUnion (messageArea));   // bar and percentage
        if (finished)
            finish();
    }

    void ExportAudioDialog::finish()
    {
        stopTimer();
        worker.join();
        if (result.cancelled)
        {
            message = "Export cancelled. No file was written.";
            messageIsError = false;
            return setState (State::choosing);
        }
        messageIsError = ! result.error.empty();
        message = messageIsError ? juce::String ("Export failed: ") + juce::String::fromUTF8 (result.error.c_str())
                                 : describe (result, running, target) + "\nSaved in " + target.getParentDirectory().getFullPathName();
        setState (messageIsError ? State::choosing : State::done);
    }

    void ExportAudioDialog::setState (State s)
    {
        state = s;
        for (auto* c : { &rangeBox, &rateBox, &bitsBox })
            c->setEnabled (s == State::choosing);
        exportButton.setVisible (s != State::rendering);
        exportButton.setButtonText (s == State::done ? "Done" : "Export...");
        cancelButton.setVisible (s != State::done);
        repaint();
    }

    juce::String ExportAudioDialog::describe (const project::AudioExportResult& r, const project::AudioExportOptions& o, const juce::File& file)
    {
        const auto peakDb = r.peak > 0.0f ? 20.0f * std::log10 (r.peak) : -144.0f;
        auto text = file.getFileName() + ": " + clock (r.seconds())
                    + (r.frames > r.musicFrames ? " (" + juce::String (r.musicSeconds(), 1) + " s + " + juce::String (r.seconds() - r.musicSeconds(), 1) + " s tail)" : juce::String())
                    + ", " + rateName (o.sampleRate) + " " + formatName (o.format)
                    + ", peak " + juce::String (peakDb, 1) + " dBFS";
        if (r.samplesOverFullScale > 0)
            text += o.format == SampleFormat::float32
                        ? "\nPeaks above 0 dBFS are kept in 32-bit float; lower the volume before converting."
                        : "\nWarning: " + juce::String (r.samplesOverFullScale) + " samples clipped. Lower the track volumes, or use 32-bit float.";
        return text;
    }

    void ExportAudioDialog::paint (juce::Graphics& g)
    {
        g.fillAll (theme::panel);

        g.setColour (theme::textPrimary);
        g.setFont (theme::font (17.0f, juce::Font::bold));
        g.drawText ("Export Audio", getLocalBounds().reduced (24, 0).removeFromTop (56), juce::Justification::centredLeft);

        g.setColour (theme::textSecondary);
        g.setFont (theme::captionFont());
        for (auto [box, label] : { std::pair { &rangeBox, "RANGE" }, std::pair { &rateBox, "SAMPLE RATE" }, std::pair { &bitsBox, "BIT DEPTH" } })
            g.drawText (label, box->getBounds().withX (24).withWidth (110), juce::Justification::centredLeft);

        if (state == State::rendering)
        {
            const auto f = juce::jlimit (0.0f, 1.0f, progress.fraction.load());
            auto bar = progressArea.toFloat();
            g.setColour (theme::control);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setGradientFill (juce::ColourGradient (theme::purple, bar.getX(), 0.0f, theme::pink, bar.getRight(), 0.0f, false));
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (8.0f, bar.getWidth() * f)), 4.0f);
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (12.5f));
            g.drawText ("Rendering " + target.getFileName() + "...  " + juce::String ((int) std::round (f * 100.0f)) + "%",
                        messageArea, juce::Justification::topLeft);
        }
        else if (message.isNotEmpty())
        {
            g.setColour (messageIsError ? theme::error : state == State::done ? theme::textPrimary : theme::textSecondary);
            g.setFont (theme::font (13.0f));
            g.drawFittedText (message, messageArea, juce::Justification::topLeft, 4);
        }
    }

    void ExportAudioDialog::resized()
    {
        auto area = getLocalBounds().reduced (24, 0);
        area.removeFromTop (56);
        for (auto* box : { &rangeBox, &rateBox, &bitsBox })
        {
            auto row = area.removeFromTop (34);
            box->setBounds (row.withTrimmedLeft (120).reduced (0, 2));
            area.removeFromTop (8);
        }
        area.removeFromTop (6);
        progressArea = area.removeFromTop (10);
        area.removeFromTop (10);
        auto buttons = area.removeFromBottom (56).withSizeKeepingCentre (area.getWidth(), 34);
        messageArea = area;
        exportButton.setBounds (buttons.removeFromRight (120));
        buttons.removeFromRight (10);
        cancelButton.setBounds (buttons.removeFromRight (100));
    }
}
