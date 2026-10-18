#include "MainComponent.h"
#include "audio/AudioDeviceController.h"
#include "ui/BeatForgeLookAndFeel.h"
#include "ui/Theme.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/project/Session.h>

#include <iostream>

namespace beatforge
{
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, audio::AudioEngine& engine, AudioDeviceController& devices,
                    project::Session& s, juce::PropertiesFile& settings)
            : DocumentWindow (name, theme::background, allButtons), session (s)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, devices, session, settings), true);
            setResizable (true, true);
            setResizeLimits (1080, 640, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            getContentComponent()->grabKeyboardFocus();

            updateTitle();
            listenerId = session.addListener ([this] (project::Session::Change change)
            {
                if (change == project::Session::Change::replaced || change == project::Session::Change::metadata)
                    updateTitle();
            });
        }

        ~MainWindow() override
        {
            session.removeListener (listenerId);
        }

        // "*Neon Horizon - BeatForge" while there are unsaved changes.
        void updateTitle()
        {
            setName ((session.isDirty() ? "*" : "") + juce::String::fromUTF8 (session.getProject().name.c_str())
                     + " - BeatForge");
        }

        MainComponent& getMainComponent() { return *static_cast<MainComponent*> (getContentComponent()); }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress ('q', juce::ModifierKeys::commandModifier, 0))
            {
                juce::JUCEApplication::getInstance()->systemRequestedQuit();
                return true;
            }

            return getMainComponent().handleShortcut (key) || DocumentWindow::keyPressed (key);
        }

        bool keyStateChanged (bool isKeyDown) override
        {
            getMainComponent().getComputerKeys().keyStateChanged();   // no component focused
            return DocumentWindow::keyStateChanged (isKeyDown);
        }

    private:
        project::Session& session;
        int listenerId = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    // --smoke-test: launch normally, confirm the window reached the screen,
    // then quit. Exit code 0 means the app started and shut down cleanly.
    // --screenshot=<file.png> additionally saves the window content (taken
    // mid-playback with --smoke-play).
    // --smoke-play additionally presses Play, checks the device clock keeps
    // real time for 2 s and the metronome fired every beat, presses Stop and
    // checks the transport stops.
    // None of this proves sound reached the speakers - only a listener can.
    class SmokeTest final : private juce::Timer
    {
    public:
        SmokeTest (MainWindow& w, audio::AudioEngine& e, AudioDeviceController& d,
                   juce::File screenshotFile, bool shouldPlay)
            : window (w), engine (e), devices (d), screenshot (std::move (screenshotFile)), play (shouldPlay)
        {
            startTimer (50);
        }

    private:
        enum class Step { waitForWindow, settling, playing, stopping };

        void timerCallback() override
        {
            const auto now = juce::Time::getMillisecondCounterHiRes();

            switch (step)
            {
                case Step::waitForWindow:
                    if (window.isShowing() && window.getPeer() != nullptr)
                        windowShown();
                    else if (now - startMs > 10000)
                        fail ("window did not appear within 10 s");
                    break;

                case Step::settling:
                    if (now - stepStartMs >= 1000)
                        pressPlay();
                    break;

                case Step::playing:
                    if (! screenshotTaken && now - stepStartMs >= 1000 && screenshot != juce::File())
                    {
                        screenshotTaken = true;   // mid-playback, so the meters are live
                        if (! saveScreenshot())
                            juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);
                    }

                    if (now - stepStartMs >= 2000)
                        checkPlaybackAndStop (now);
                    break;

                case Step::stopping:
                    if (engine.getState() == audio::TransportState::stopped)
                        finish();
                    else if (now - stepStartMs > 1000)
                        fail ("transport did not reach STOPPED within 1 s of Stop");
                    break;
            }
        }

        void windowShown()
        {
            std::cout << "SMOKE: window shown " << window.getWidth() << "x" << window.getHeight() << std::endl;

            const auto s = devices.getStatus();
            if (s.deviceOpen)
                std::cout << "SMOKE: audio device " << s.typeName << " / " << s.deviceName << " @ "
                          << s.sampleRate << " Hz, " << s.bufferSize << " samples" << std::endl;
            else
                std::cout << "SMOKE: no audio device (" << s.error << ")" << std::endl;

            if (! play)
            {
                if (screenshot != juce::File() && ! saveScreenshot())
                    juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);

                return finish();
            }

            if (! s.deviceOpen)
                return fail ("--smoke-play needs an open audio device");

            // Give external recorders (pw-record) a moment to attach to our stream.
            step = Step::settling;
            stepStartMs = juce::Time::getMillisecondCounterHiRes();
        }

        void pressPlay()
        {
            beatsAtStart = engine.getBeatCount();
            window.getMainComponent().getTransportBar().togglePlayStop();   // same path as Space
            step = Step::playing;
            stepStartMs = juce::Time::getMillisecondCounterHiRes();
        }

        void checkPlaybackAndStop (double now)
        {
            const auto audioSeconds = (double) engine.getPositionSamples() / engine.getSampleRate();
            const auto wallSeconds = (now - stepStartMs) / 1000.0;
            const auto ratio = audioSeconds / wallSeconds;

            const auto beats = engine.getBeatCount() - beatsAtStart;
            const auto expectedBeats = (std::uint64_t) (audioSeconds * engine.getTempo() / 60.0) + 1;

            std::cout << "SMOKE: played " << juce::String (wallSeconds, 2) << " s wall clock, "
                      << juce::String (audioSeconds, 2) << " s of audio rendered (ratio "
                      << juce::String (ratio, 3) << "), " << beats << " beats at "
                      << engine.getTempo() << " BPM, dropouts " << devices.getStatus().xruns << std::endl;

            if (engine.getState() != audio::TransportState::playing)
                return fail ("transport is not PLAYING after Play");

            if (beats != expectedBeats)
                return fail ("expected " + juce::String ((juce::int64) expectedBeats) + " metronome beats");

            // The first device buffer(s) start a little after Play, so allow slack.
            if (ratio < 0.85 || ratio > 1.10)
                return fail ("audio clock does not keep real time");

            window.getMainComponent().getTransportBar().togglePlayStop();
            step = Step::stopping;
            stepStartMs = now;
        }

        bool saveScreenshot()
        {
            auto* content = window.getContentComponent();
            auto image = content->createComponentSnapshot (content->getLocalBounds());
            screenshot.deleteFile();
            juce::FileOutputStream out (screenshot);

            if (out.openedOk() && juce::PNGImageFormat().writeImageToStream (image, out))
            {
                std::cout << "SMOKE: screenshot saved to " << screenshot.getFullPathName() << std::endl;
                return true;
            }

            std::cerr << "SMOKE: could not write screenshot to " << screenshot.getFullPathName() << std::endl;
            return false;
        }

        void finish()
        {
            stopTimer();
            if (play)
                std::cout << "SMOKE: playback stopped cleanly" << std::endl;
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        void fail (const juce::String& why)
        {
            stopTimer();
            std::cerr << "SMOKE: FAILED - " << why << std::endl;
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);
            juce::JUCEApplication::quit();
        }

        MainWindow& window;
        audio::AudioEngine& engine;
        AudioDeviceController& devices;
        juce::File screenshot;
        bool play;
        bool screenshotTaken = false;
        std::uint64_t beatsAtStart = 0;

        Step step = Step::waitForWindow;
        double startMs = juce::Time::getMillisecondCounterHiRes();
        double stepStartMs = 0.0;
    };

    class BeatForgeApplication final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        void initialise (const juce::String&) override
        {
            const auto args = getCommandLineParameterArray();

            if (args.contains ("--version"))
            {
                std::cout << "BeatForge " << getApplicationVersion() << std::endl;
                quit();
                return;
            }

            juce::PropertiesFile::Options options;
            options.applicationName = "BeatForge";
           #if JUCE_LINUX
            // JUCE places folderName directly under $HOME on Linux; follow XDG instead.
            options.folderName = ".config/BeatForge";
           #else
            options.folderName = "BeatForge";
           #endif
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            migrateOldSettings (options);
            settings.setStorageParameters (options);

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            engine = std::make_unique<audio::AudioEngine>();
            devices = std::make_unique<AudioDeviceController> (*engine, *settings.getUserSettings());
            devices->open();

            session = std::make_unique<project::Session> (*engine);
            mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *devices, *session,
                                                       *settings.getUserSettings());

            const bool smokePlay = args.contains ("--smoke-play");
            auto& actions = mainWindow->getMainComponent().getProjectActions();
            actions.setReportToConsole (args.contains ("--smoke-test") || smokePlay);

            // A project file on the command line opens at launch.
            for (auto& arg : args)
                if (! arg.startsWith ("--"))
                    actions.openFile (juce::File::getCurrentWorkingDirectory().getChildFile (arg.unquoted()));

            // --export-wav=<file.wav> [--wav-rate=48000] [--wav-bits=16|24|32] [--wav-range=song|loop]
            for (auto& arg : args)
                if (arg.startsWith ("--export-wav="))
                {
                    project::AudioExportOptions o;
                    for (auto& opt : args)
                    {
                        if (opt.startsWith ("--wav-rate="))  o.sampleRate = opt.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
                        if (opt.startsWith ("--wav-bits="))
                        {
                            const auto bits = opt.fromFirstOccurrenceOf ("=", false, false).getIntValue();
                            o.format = bits == 16 ? audio::SampleFormat::int16 : bits == 32 ? audio::SampleFormat::float32 : audio::SampleFormat::int24;
                        }
                        if (opt == "--wav-range=loop") o.range = project::AudioExportOptions::Range::loop;
                    }
                    actions.exportAudioTo (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()), o);
                }

            // --export-midi=<file.mid>: export the (opened) project's MIDI at launch.
            for (auto& arg : args)
                if (arg.startsWith ("--export-midi="))
                    actions.exportMidiTo (juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false).unquoted()));

            if (args.contains ("--smoke-test") || smokePlay)
            {
                juce::File screenshot;

                for (auto& arg : args)
                    if (arg.startsWith ("--screenshot="))
                        screenshot = juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));

                smokeTest = std::make_unique<SmokeTest> (*mainWindow, *engine, *devices, screenshot, smokePlay);
            }
        }

        void shutdown() override
        {
            smokeTest = nullptr;
            mainWindow = nullptr;
            session = nullptr;
            devices = nullptr;   // closes the device: no more audio callbacks after this
            engine = nullptr;
            settings.closeFiles();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            std::cout << "BeatForge shut down cleanly" << std::endl;
        }

        // Unsaved changes are offered for saving first; Cancel keeps the app open.
        // Versions up to 0.3 kept settings in ~/BeatForge/; move them once.
        static void migrateOldSettings (const juce::PropertiesFile::Options& options)
        {
           #if JUCE_LINUX
            const auto oldFolder = juce::File ("~/BeatForge");
            const auto oldFile = oldFolder.getChildFile ("BeatForge.settings");
            const auto newFile = options.getDefaultFile();

            if (oldFile.existsAsFile() && ! newFile.exists())
            {
                newFile.getParentDirectory().createDirectory();
                if (oldFile.moveFileTo (newFile) && oldFolder.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
                    oldFolder.deleteFile();
            }
           #else
            juce::ignoreUnused (options);
           #endif
        }

        void systemRequestedQuit() override
        {
            if (mainWindow == nullptr)
                return quit();

            mainWindow->getMainComponent().getProjectActions().confirmDiscardChanges ([] { quit(); });
        }

    private:
        BeatForgeLookAndFeel lookAndFeel;
        juce::ApplicationProperties settings;
        std::unique_ptr<audio::AudioEngine> engine;
        std::unique_ptr<AudioDeviceController> devices;
        std::unique_ptr<project::Session> session;
        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<SmokeTest> smokeTest;
    };
}

START_JUCE_APPLICATION (beatforge::BeatForgeApplication)
