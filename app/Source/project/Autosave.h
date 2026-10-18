#pragma once

#include <beatforge/project/Session.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace beatforge
{
    // Crash protection: while the project has unsaved changes it is written
    // to a recovery file every minute (never over your own file). Saving, or
    // closing BeatForge normally, removes it, so it only survives a crash; at
    // the next start the user is offered to restore it.
    class Autosave final : private juce::Timer
    {
    public:
        Autosave (project::Session&, juce::PropertiesFile& settings);
        ~Autosave() override;   // normal shutdown: the user saved or chose not to

        void start();

        struct Recovery
        {
            project::Project project;
            std::optional<std::filesystem::path> originalFile;
            juce::Time savedAt;
        };
        // A recovery file left by a crash (nullopt if none or unreadable).
        std::optional<Recovery> pendingRecovery() const;
        void discard();

        // For the save indicator.
        juce::Time getLastAutosave() const noexcept { return lastWrite; }
        bool isCurrentAutosaved() const;   // the unsaved changes as they are now are in the recovery file

        std::function<void()> onAutosaved;

    private:
        void timerCallback() override;
        void write();

        project::Session& session;
        juce::PropertiesFile& settings;
        juce::File file;
        int listenerId = 0;
        std::optional<project::Project> written;   // what is in the recovery file
        juce::Time lastWrite, dirtySince;
        int intervalSeconds = 60;
    };
}
