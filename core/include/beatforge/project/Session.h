#pragma once

#include "beatforge/audio/AudioEngine.h"
#include "beatforge/audio/InstrumentPresets.h"
#include "beatforge/project/Project.h"

#include <array>
#include <deque>
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
    // these methods: they validate, update the Project, record an undo step,
    // push the change into the AudioEngine and notify listeners. Edits that
    // change nothing are ignored. Message-thread only.
    class Session
    {
    public:
        enum class Change
        {
            replaced,     // new/opened project, undo/redo: refresh everything
            transport,    // tempo, signature, loop, metronome
            tracks,       // tracks added/removed/renamed, mix, drum lane settings
            clips,        // clips created/moved/resized/deleted, steps, notes
            instrument,   // instrument choice or settings
            metadata      // name, file, saved/unsaved, undo availability
        };

        explicit Session (audio::AudioEngine&);   // starts with Project::makeDefault()

        const Project& getProject() const noexcept { return project; }
        const Track* findTrack (TrackId) const noexcept;
        const Clip* findClip (ClipId) const noexcept;
        TrackId trackOfClip (ClipId) const noexcept;
        int slotOf (TrackId) const noexcept;   // engine slot, -1 if none
        const midi::Note* findNote (ClipId, midi::NoteId) const noexcept;

        // --- Project and transport ------------------------------------------------
        void setTempo (double bpm);
        void setTimeSignature (time::TimeSignature);
        void setMetronomeEnabled (bool);
        void setMasterVolume (float db);
        void setLoop (bool enabled, time::Tick start, time::Tick end);   // ignored unless start < end
        bool renameProject (const std::string&);

        // --- Tracks -----------------------------------------------------------------
        TrackId addTrack (TrackType);            // 0 when the 16-track limit is reached
        bool removeTrack (TrackId);
        bool renameTrack (TrackId, const std::string&);
        // Moves a track to `newIndex` in the track list (clamped); its sound is unaffected.
        bool moveTrack (TrackId, int newIndex);
        void setTrackVolume (TrackId, float db);
        void setTrackPan (TrackId, float pan);
        void setTrackMuted (TrackId, bool);
        void setTrackSoloed (TrackId, bool);

        void setLaneGainDb (TrackId, int lane, float db);
        void setLaneMuted (TrackId, int lane, bool);
        void setLaneSoloed (TrackId, int lane, bool);
        bool renameLane (TrackId, int lane, const std::string&);

        void setInstrument (TrackId, audio::InstrumentType);
        void setInstrumentParam (TrackId, audio::InstrumentType, audio::Param, float value);
        float getInstrumentParam (TrackId, audio::InstrumentType, audio::Param) const noexcept;
        void resetInstrumentParams (TrackId, audio::InstrumentType);
        // Loads a factory sound into the track's instrument (one undo step, "Preset: <name>").
        bool applyPreset (TrackId, const audio::InstrumentPreset&);

        // --- Clips --------------------------------------------------------------------
        ClipId createClip (TrackId, time::Tick start, time::Tick length);   // 0 if invalid
        bool deleteClip (ClipId);
        // Moves a clip in time and optionally to another track of the same type.
        void moveClip (ClipId, time::Tick newStart, TrackId toTrack = 0);
        void resizeClip (ClipId, time::Tick newLength);
        ClipId duplicateClip (ClipId);           // copy placed right after the original
        // A copy of `content` (from a track of `contentType`) on another track at `start`; 0 if the types differ.
        ClipId pasteClip (TrackId, const Clip& content, TrackType contentType, time::Tick start);

        void setStep (ClipId, int lane, int step, bool on);
        void clearPattern (ClipId);

        midi::NoteId addNote (ClipId, midi::Note);   // 0 if the clip is not an instrument clip
        void updateNotes (ClipId, const std::vector<midi::Note>&);
        void removeNotes (ClipId, const std::vector<midi::NoteId>&);
        void quantizeNotes (ClipId, const std::vector<midi::NoteId>& ids, time::Tick grid);   // empty ids = all

        // --- Undo -----------------------------------------------------------------------
        // Edits between begin and end (e.g. a drag) become one undo step,
        // named `label` (else after its first edit).
        void beginGesture (std::string label = {});
        void endGesture();
        bool canUndo() const noexcept { return ! undoStack.empty(); }
        bool canRedo() const noexcept { return ! redoStack.empty(); }
        void undo();
        void redo();
        // What Undo / Redo would do, e.g. "Move clip" (empty if nothing).
        std::string undoLabel() const { return undoStack.empty() ? std::string() : undoStack.back().label; }
        std::string redoLabel() const { return redoStack.empty() ? std::string() : redoStack.back().label; }

        // --- Lifecycle --------------------------------------------------------------------
        void newProject();
        void openProject (Project, std::optional<std::filesystem::path> file);
        void markSaved (const std::filesystem::path& file);
        // The open project counts as unsaved (e.g. restored after a crash).
        void markUnsaved();
        bool isDirty() const noexcept { return ! (project == savedProject); }
        const std::optional<std::filesystem::path>& getFile() const noexcept { return file; }

        // --- Listeners ----------------------------------------------------------------------
        using Listener = std::function<void (Change)>;
        int addListener (Listener);
        void removeListener (int id);

        static constexpr size_t maxUndoSteps = 200;

    private:
        Track* track (TrackId) noexcept;
        Clip* clip (ClipId) noexcept;
        Track* trackWithClip (ClipId) noexcept;

        // Applies `mutate` (returns false = nothing changed) as one undoable edit.
        template <typename Mutate>
        bool edit (Change, const char* label, Mutate&& mutate);

        void replaceProject (Project);
        void syncEngine (Change);
        void assignSlots();
        void refreshIds();
        void notify (Change, bool wasDirty, bool couldUndo, bool couldRedo);

        audio::AudioEngine& engine;
        Project project, savedProject;
        std::optional<std::filesystem::path> file;

        struct UndoStep
        {
            Project project;
            std::string label;
        };
        std::deque<UndoStep> undoStack, redoStack;
        std::string gestureLabel;
        int gestureDepth = 0;
        bool gestureRecorded = false;

        std::array<TrackId, audio::AudioEngine::maxTracks> slotTracks {};   // 0 = free
        TrackId nextTrackId = 1;
        ClipId nextClipId = 1;
        midi::NoteId nextNoteId = 1;

        std::vector<std::pair<int, Listener>> listeners;
        int nextListenerId = 1;
    };
}
