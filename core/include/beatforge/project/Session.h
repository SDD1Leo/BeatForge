#pragma once

#include "beatforge/audio/AudioEngine.h"
#include "beatforge/project/Project.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace beatforge::project
{
    // The open project and the single way to change it.
    //
    // Every edit (from the GUI today, from MCP/an assistant later) goes through
    // these methods: they validate, update the Project, push the change into the
    // AudioEngine and notify listeners. Message-thread only.
    class Session
    {
    public:
        enum class Change
        {
            replaced,     // new/opened project: refresh everything
            transport,    // tempo, signature, loop, metronome
            pattern,      // drum steps
            lanes,        // lane names, volume, mute, solo
            metadata      // name, file path, saved/unsaved
        };

        explicit Session (audio::AudioEngine&);

        const Project& getProject() const noexcept { return project; }

        // --- Edits ----------------------------------------------------------
        void setTempo (double bpm);
        void setTimeSignature (time::TimeSignature);
        void setMetronomeEnabled (bool);
        void setLoop (bool enabled, int bars);

        void setStep (int lane, int step, bool on);
        void clearPattern();

        void setLaneGainDb (int lane, float db);
        void setLaneMuted (int lane, bool);
        void setLaneSoloed (int lane, bool);
        // Returns false (and changes nothing) for an empty or over-long name.
        bool renameLane (int lane, const std::string& name);
        bool renameProject (const std::string& name);

        // --- Lifecycle --------------------------------------------------------
        void newProject();
        // Replaces the project (after loading); `file` is where it came from.
        void openProject (Project, std::optional<std::filesystem::path> file);
        void markSaved (const std::filesystem::path& file);

        bool isDirty() const noexcept { return dirty; }
        const std::optional<std::filesystem::path>& getFile() const noexcept { return file; }

        // --- Listeners --------------------------------------------------------
        using Listener = std::function<void (Change)>;
        int addListener (Listener);
        void removeListener (int id);

    private:
        void applyAllToEngine();
        void applyLoop();
        void changed (Change, bool makesDirty = true);
        static bool validLane (int lane) noexcept;

        audio::AudioEngine& engine;
        Project project;
        std::optional<std::filesystem::path> file;
        bool dirty = false;

        std::vector<std::pair<int, Listener>> listeners;
        int nextListenerId = 1;
    };
}
