#include "MainComponent.h"
#include "ui/BeatForgeLookAndFeel.h"
#include "ui/Theme.h"

#include <iostream>

namespace beatforge
{
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, theme::background, allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (960, 600, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

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
    // --screenshot=<file.png> additionally saves the window content first.
    class SmokeTest final : private juce::Timer
    {
    public:
        SmokeTest (juce::DocumentWindow& windowToCheck, juce::File screenshotFile)
            : window (windowToCheck), screenshot (std::move (screenshotFile))
        {
            startTimer (50);
        }

    private:
        void timerCallback() override
        {
            if (window.isShowing() && window.getPeer() != nullptr)
            {
                stopTimer();
                std::cout << "SMOKE: window shown " << window.getWidth() << "x" << window.getHeight() << std::endl;

                if (screenshot != juce::File() && ! saveScreenshot())
                    juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);

                juce::JUCEApplication::getInstance()->systemRequestedQuit();
            }
            else if (++ticks * 50 > timeoutMs)
            {
                stopTimer();
                std::cerr << "SMOKE: window did not appear within " << timeoutMs << " ms" << std::endl;
                juce::JUCEApplication::getInstance()->setApplicationReturnValue (1);
                juce::JUCEApplication::quit();
            }
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

        static constexpr int timeoutMs = 10000;
        juce::DocumentWindow& window;
        juce::File screenshot;
        int ticks = 0;
    };

    class BeatForgeApplication final : public juce::JUCEApplication
    {
    public:
        const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
        const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
        bool moreThanOneInstanceAllowed() override          { return true; }

        void initialise (const juce::String&) override
        {
            if (getCommandLineParameterArray().contains ("--version"))
            {
                std::cout << "BeatForge " << getApplicationVersion() << std::endl;
                quit();
                return;
            }

            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
            mainWindow = std::make_unique<MainWindow> (getApplicationName());

            const auto args = getCommandLineParameterArray();

            if (args.contains ("--smoke-test"))
            {
                juce::File screenshot;

                for (auto& arg : args)
                    if (arg.startsWith ("--screenshot="))
                        screenshot = juce::File::getCurrentWorkingDirectory().getChildFile (arg.fromFirstOccurrenceOf ("=", false, false));

                smokeTest = std::make_unique<SmokeTest> (*mainWindow, screenshot);
            }
        }

        void shutdown() override
        {
            smokeTest = nullptr;
            mainWindow = nullptr;
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            std::cout << "BeatForge shut down cleanly" << std::endl;
        }

        void systemRequestedQuit() override { quit(); }

    private:
        BeatForgeLookAndFeel lookAndFeel;
        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<SmokeTest> smokeTest;
    };
}

START_JUCE_APPLICATION (beatforge::BeatForgeApplication)
