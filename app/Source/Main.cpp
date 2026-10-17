#include "MainComponent.h"
#include "audio/AudioDeviceController.h"
#include "ui/BeatForgeLookAndFeel.h"
#include "ui/Theme.h"

#include <beatforge/audio/AudioEngine.h>

#include <iostream>

namespace beatforge
{
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, audio::AudioEngine& engine, AudioDeviceController& devices)
            : DocumentWindow (name, theme::background, allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (engine, devices), true);
            setResizable (true, true);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            getContentComponent()->grabKeyboardFocus();
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

            return DocumentWindow::keyPressed (key);
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    // --smoke-test: launch normally, confirm the window reached the screen,
    // then quit. Exit code 0 means the app started and shut down cleanly.
    // --screenshot=<file.png> additionally saves the window content (taken
    // mid-playback with --smoke-play).
    // --smoke-play additionally presses Play, checks the device clock keeps
    // real time for 2 s, presses Stop and checks the transport stops.
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
        enum class Step { waitForWindow, playing, stopping };

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

            window.getMainComponent().getTransportBar().togglePlayStop();   // same path as Space
            step = Step::playing;
            stepStartMs = juce::Time::getMillisecondCounterHiRes();
        }

        void checkPlaybackAndStop (double now)
        {
            const auto audioSeconds = (double) engine.getPositionSamples() / engine.getSampleRate();
            const auto wallSeconds = (now - stepStartMs) / 1000.0;
            const auto ratio = audioSeconds / wallSeconds;

            std::cout << "SMOKE: played " << juce::String (wallSeconds, 2) << " s wall clock, "
                      << juce::String (audioSeconds, 2) << " s of audio rendered (ratio "
                      << juce::String (ratio, 3) << "), dropouts " << devices.getStatus().xruns << std::endl;

            if (engine.getState() != audio::TransportState::playing)
                return fail ("transport is not PLAYING after Play");

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
            options.folderName = "BeatForge";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            settings.setStorageParameters (options);

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

            engine = std::make_unique<audio::AudioEngine>();
            devices = std::make_unique<AudioDeviceController> (*engine, *settings.getUserSettings());
            devices->open();

            mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *devices);

            const bool smokePlay = args.contains ("--smoke-play");

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
            devices = nullptr;   // closes the device: no more audio callbacks after this
            engine = nullptr;
            settings.closeFiles();
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            std::cout << "BeatForge shut down cleanly" << std::endl;
        }

        void systemRequestedQuit() override { quit(); }

    private:
        BeatForgeLookAndFeel lookAndFeel;
        juce::ApplicationProperties settings;
        std::unique_ptr<audio::AudioEngine> engine;
        std::unique_ptr<AudioDeviceController> devices;
        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<SmokeTest> smokeTest;
    };
}

START_JUCE_APPLICATION (beatforge::BeatForgeApplication)
