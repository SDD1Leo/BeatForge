#include "beatforge/project/Session.h"

#include "beatforge/project/ScheduleBuilder.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace beatforge::project
{
    namespace
    {
        bool validLane (int lane) noexcept { return lane >= 0 && lane < audio::DrumMachine::numLanes; }

        void sortClips (Track& t)
        {
            std::stable_sort (t.clips.begin(), t.clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
        }

        template <typename T>
        bool assign (T& target, const T& value)
        {
            if (target == value)
                return false;
            target = value;
            return true;
        }
    }

    Session::Session (audio::AudioEngine& e) : engine (e)
    {
        replaceProject (Project::makeDefault());
        savedProject = project;
    }

    //==============================================================================
    const Track* Session::findTrack (TrackId id) const noexcept
    {
        for (auto& t : project.tracks)
            if (t.id == id)
                return &t;
        return nullptr;
    }

    const Clip* Session::findClip (ClipId id) const noexcept
    {
        for (auto& t : project.tracks)
            for (auto& c : t.clips)
                if (c.id == id)
                    return &c;
        return nullptr;
    }

    TrackId Session::trackOfClip (ClipId id) const noexcept
    {
        for (auto& t : project.tracks)
            for (auto& c : t.clips)
                if (c.id == id)
                    return t.id;
        return 0;
    }

    int Session::slotOf (TrackId id) const noexcept
    {
        for (size_t i = 0; i < slotTracks.size(); ++i)
            if (slotTracks[i] == id && id != 0)
                return (int) i;
        return -1;
    }

    const midi::Note* Session::findNote (ClipId c, midi::NoteId n) const noexcept
    {
        if (auto* cl = findClip (c))
            for (auto& note : cl->notes)
                if (note.id == n)
                    return &note;
        return nullptr;
    }

    Track* Session::track (TrackId id) noexcept         { return const_cast<Track*> (std::as_const (*this).findTrack (id)); }
    Clip* Session::clip (ClipId id) noexcept            { return const_cast<Clip*> (std::as_const (*this).findClip (id)); }

    Track* Session::trackWithClip (ClipId id) noexcept
    {
        const auto t = trackOfClip (id);
        return t != 0 ? track (t) : nullptr;
    }

    //==============================================================================
    template <typename Mutate>
    bool Session::edit (Change change, const char* label, Mutate&& mutate)
    {
        const bool wasDirty = isDirty(), couldUndo = canUndo(), couldRedo = canRedo();
        Project before = project;

        if (! mutate (project))
            return false;

        if (gestureDepth == 0 || ! gestureRecorded)
        {
            undoStack.push_back ({ std::move (before), gestureDepth > 0 && ! gestureLabel.empty() ? gestureLabel : std::string (label) });
            if (undoStack.size() > maxUndoSteps)
                undoStack.pop_front();
            gestureRecorded = gestureDepth > 0;
        }
        redoStack.clear();

        syncEngine (change);
        notify (change, wasDirty, couldUndo, couldRedo);
        return true;
    }

    void Session::notify (Change change, bool wasDirty, bool couldUndo, bool couldRedo)
    {
        auto current = listeners;   // a listener may add/remove listeners
        const bool metadataChanged = wasDirty != isDirty() || couldUndo != canUndo() || couldRedo != canRedo();

        for (auto& [id, listener] : current)
        {
            listener (change);
            if (metadataChanged && change != Change::metadata)
                listener (Change::metadata);
        }
    }

    //==============================================================================
    void Session::assignSlots()
    {
        // Free slots of tracks that no longer exist.
        for (auto& s : slotTracks)
            if (s != 0 && findTrack (s) == nullptr)
                s = 0;

        // Give every track a slot, keeping existing ones (their voices keep ringing).
        for (auto& t : project.tracks)
            if (slotOf (t.id) < 0)
                for (auto& s : slotTracks)
                    if (s == 0) { s = t.id; break; }
    }

    void Session::syncEngine (Change change)
    {
        engine.setTempo (project.tempo);
        engine.setTimeSignature (project.timeSignature);
        engine.setMetronomeEnabled (project.metronomeEnabled);
        engine.setMasterVolume (project.masterVolumeDb);
        engine.setLoop (project.loopEnabled, project.loopStart, project.loopEnd);

        if (change == Change::transport || change == Change::metadata)
            return;

        assignSlots();

        for (int slot = 0; slot < audio::AudioEngine::maxTracks; ++slot)
        {
            const auto* t = findTrack (slotTracks[(size_t) slot]);
            if (t == nullptr)
            {
                if (engine.getSlotType (slot) != audio::SlotType::none)
                {
                    engine.setSlotType (slot, audio::SlotType::none);
                    engine.setSlotSchedule (slot, nullptr);
                }
                continue;
            }

            engine.setSlotType (slot, t->type == TrackType::drums ? audio::SlotType::drums : audio::SlotType::instrument);
            engine.setSlotMix (slot, t->volumeDb, t->pan, t->muted, t->soloed);

            if (t->type == TrackType::drums)
            {
                auto& drums = engine.drums (slot);
                for (int lane = 0; lane < audio::DrumMachine::numLanes; ++lane)
                {
                    const auto& l = t->lanes[(size_t) lane];
                    drums.setLaneGainDb (lane, l.gainDb);
                    drums.setLaneMuted (lane, l.muted);
                    drums.setLaneSoloed (lane, l.soloed);
                }
            }
            else
            {
                auto& rack = engine.instruments (slot);
                rack.setType (t->instrument);
                for (int type = 0; type < audio::numInstrumentTypes; ++type)
                    for (int p = 0; p < audio::numParams; ++p)
                        rack.params ((audio::InstrumentType) type).set ((audio::Param) p, t->instrumentParams[(size_t) type][(size_t) p]);
            }

            if (change == Change::clips || change == Change::replaced)
                engine.setSlotSchedule (slot, buildSchedule (*t));
        }
    }

    void Session::refreshIds()
    {
        nextTrackId = nextClipId = nextNoteId = 1;
        for (auto& t : project.tracks)
        {
            nextTrackId = std::max (nextTrackId, t.id + 1);
            for (auto& c : t.clips)
            {
                nextClipId = std::max (nextClipId, c.id + 1);
                for (auto& n : c.notes)
                    nextNoteId = std::max (nextNoteId, n.id + 1);
            }
        }
    }

    void Session::replaceProject (Project p)
    {
        project = std::move (p);

        // Files carry no ids; give anything without one a fresh id.
        refreshIds();
        for (auto& t : project.tracks)
        {
            if (t.id == 0) t.id = nextTrackId++;
            for (auto& c : t.clips)
            {
                if (c.id == 0) c.id = nextClipId++;
                for (auto& n : c.notes)
                    if (n.id == 0) n.id = nextNoteId++;
            }
            sortClips (t);
        }
        refreshIds();
        syncEngine (Change::replaced);
    }

    //==============================================================================
    void Session::setMasterVolume (float db)
    {
        db = std::clamp (db, Project::minVolumeDb, Project::maxVolumeDb);
        edit (Change::tracks, "Master volume", [&] (Project& p) { return assign (p.masterVolumeDb, db); });
    }

    void Session::setTempo (double bpm)
    {
        bpm = time::clampTempo (bpm);
        edit (Change::transport, "Change tempo", [&] (Project& p) { return assign (p.tempo, bpm); });
    }

    void Session::setTimeSignature (time::TimeSignature sig)
    {
        if (sig.isValid())
            edit (Change::transport, "Change time signature", [&] (Project& p) { return assign (p.timeSignature, sig); });
    }

    void Session::setMetronomeEnabled (bool enabled)
    {
        edit (Change::transport, "Metronome on/off", [&] (Project& p) { return assign (p.metronomeEnabled, enabled); });
    }

    void Session::setLoop (bool enabled, time::Tick start, time::Tick end)
    {
        if (start < 0 || end <= start)
            return;
        edit (Change::transport, "Change loop", [&] (Project& p)
        {
            const bool a = assign (p.loopEnabled, enabled);
            const bool b = assign (p.loopStart, start);
            const bool c = assign (p.loopEnd, end);
            return a || b || c;
        });
    }

    bool Session::renameProject (const std::string& name)
    {
        if (name.empty() || name.size() > (size_t) Project::maxNameLength)
            return false;
        edit (Change::metadata, "Rename project", [&] (Project& p) { return assign (p.name, name); });
        return true;
    }

    //==============================================================================
    TrackId Session::addTrack (TrackType type)
    {
        if ((int) project.tracks.size() >= Project::maxTracks)
            return 0;

        const auto id = nextTrackId;
        edit (Change::tracks, "Add track", [&] (Project& p)
        {
            Track t;
            t.id = id;
            t.type = type;

            // "Drums", "Drums 2", ... / "Synth", "Synth 2", ...
            const std::string base = type == TrackType::drums ? "Drums" : "Synth";
            int count = 0;
            for (auto& existing : p.tracks)
                count += existing.type == type ? 1 : 0;
            t.name = count == 0 ? base : base + " " + std::to_string (count + 1);

            p.tracks.push_back (t);
            return true;
        });
        ++nextTrackId;
        return id;
    }

    bool Session::removeTrack (TrackId id)
    {
        return edit (Change::clips, "Delete track", [&] (Project& p)
        {
            return std::erase_if (p.tracks, [&] (const Track& t) { return t.id == id; }) > 0;
        });
    }

    bool Session::moveTrack (TrackId id, int newIndex)
    {
        return edit (Change::tracks, "Move track", [&] (Project& p)
        {
            const auto it = std::find_if (p.tracks.begin(), p.tracks.end(), [&] (const Track& t) { return t.id == id; });
            if (it == p.tracks.end())
                return false;
            const auto from = (int) (it - p.tracks.begin());
            const auto to = std::clamp (newIndex, 0, (int) p.tracks.size() - 1);
            if (from == to)
                return false;
            auto moved = std::move (*it);
            p.tracks.erase (it);
            p.tracks.insert (p.tracks.begin() + to, std::move (moved));
            return true;
        });
    }

    bool Session::renameTrack (TrackId id, const std::string& name)
    {
        if (name.empty() || name.size() > (size_t) Project::maxNameLength || findTrack (id) == nullptr)
            return false;
        edit (Change::tracks, "Rename track", [&] (Project&) { return assign (track (id)->name, name); });
        return true;
    }

    void Session::setTrackVolume (TrackId id, float db)
    {
        if (findTrack (id) == nullptr || ! std::isfinite (db)) return;
        db = std::clamp (db, Project::minVolumeDb, Project::maxVolumeDb);
        edit (Change::tracks, "Track volume", [&] (Project&) { return assign (track (id)->volumeDb, db); });
    }

    void Session::setTrackPan (TrackId id, float pan)
    {
        if (findTrack (id) == nullptr || ! std::isfinite (pan)) return;
        pan = std::clamp (pan, -1.0f, 1.0f);
        edit (Change::tracks, "Track pan", [&] (Project&) { return assign (track (id)->pan, pan); });
    }

    void Session::setTrackMuted (TrackId id, bool m)
    {
        if (findTrack (id) != nullptr) edit (Change::tracks, "Mute track", [&] (Project&) { return assign (track (id)->muted, m); });
    }

    void Session::setTrackSoloed (TrackId id, bool s)
    {
        if (findTrack (id) != nullptr) edit (Change::tracks, "Solo track", [&] (Project&) { return assign (track (id)->soloed, s); });
    }

    void Session::setLaneGainDb (TrackId id, int lane, float db)
    {
        auto* t = findTrack (id);
        if (t == nullptr || t->type != TrackType::drums || ! validLane (lane) || ! std::isfinite (db)) return;
        db = std::clamp (db, audio::DrumMachine::minGainDb, audio::DrumMachine::maxGainDb);
        edit (Change::tracks, "Drum level", [&] (Project&) { return assign (track (id)->lanes[(size_t) lane].gainDb, db); });
    }

    void Session::setLaneMuted (TrackId id, int lane, bool m)
    {
        auto* t = findTrack (id);
        if (t != nullptr && t->type == TrackType::drums && validLane (lane))
            edit (Change::tracks, "Mute drum", [&] (Project&) { return assign (track (id)->lanes[(size_t) lane].muted, m); });
    }

    void Session::setLaneSoloed (TrackId id, int lane, bool s)
    {
        auto* t = findTrack (id);
        if (t != nullptr && t->type == TrackType::drums && validLane (lane))
            edit (Change::tracks, "Solo drum", [&] (Project&) { return assign (track (id)->lanes[(size_t) lane].soloed, s); });
    }

    bool Session::renameLane (TrackId id, int lane, const std::string& name)
    {
        auto* t = findTrack (id);
        if (t == nullptr || t->type != TrackType::drums || ! validLane (lane)
            || name.empty() || name.size() > (size_t) Project::maxLaneNameLength)
            return false;
        edit (Change::tracks, "Rename drum", [&] (Project&) { return assign (track (id)->lanes[(size_t) lane].name, name); });
        return true;
    }

    void Session::setInstrument (TrackId id, audio::InstrumentType type)
    {
        auto* t = findTrack (id);
        if (t != nullptr && t->type == TrackType::instrument)
            edit (Change::instrument, "Change instrument", [&] (Project&) { return assign (track (id)->instrument, type); });
    }

    void Session::setInstrumentParam (TrackId id, audio::InstrumentType type, audio::Param param, float value)
    {
        auto* t = findTrack (id);
        if (t == nullptr || t->type != TrackType::instrument || ! std::isfinite (value))
            return;

        const auto& spec = audio::paramSpec (param);
        if (param == audio::Param::wave)
            value = std::round (value);
        value = std::clamp (value, spec.min, spec.max);
        edit (Change::instrument, "Change sound", [&] (Project&) { return assign (track (id)->instrumentParams[(size_t) type][(size_t) param], value); });
    }

    float Session::getInstrumentParam (TrackId id, audio::InstrumentType type, audio::Param param) const noexcept
    {
        if (auto* t = findTrack (id))
            return t->instrumentParams[(size_t) type][(size_t) param];
        return audio::defaultValue (type, param);
    }

    void Session::resetInstrumentParams (TrackId id, audio::InstrumentType type)
    {
        auto* t = findTrack (id);
        if (t == nullptr || t->type != TrackType::instrument)
            return;
        const auto defaults = Track::defaultInstrumentParams()[(size_t) type];
        edit (Change::instrument, "Reset sound", [&] (Project&) { return assign (track (id)->instrumentParams[(size_t) type], defaults); });
    }

    bool Session::applyPreset (TrackId id, const audio::InstrumentPreset& preset)
    {
        auto* t = findTrack (id);
        if (t == nullptr || t->type != TrackType::instrument)
            return false;
        beginGesture ("Preset: " + std::string (preset.name));
        edit (Change::instrument, "Preset", [&] (Project&)
        {
            auto* tr = track (id);
            const bool a = assign (tr->instrument, preset.type);
            const bool b = assign (tr->instrumentParams[(size_t) preset.type], preset.values);
            return a || b;
        });
        endGesture();
        return true;
    }

    //==============================================================================
    ClipId Session::createClip (TrackId trackId, time::Tick start, time::Tick length)
    {
        if (findTrack (trackId) == nullptr || start < 0 || length < 1 || length > Project::maxClipLength)
            return 0;

        const auto id = nextClipId;
        edit (Change::clips, "Create clip", [&] (Project&)
        {
            Clip c;
            c.id = id;
            c.start = start;
            c.length = length;
            auto* t = track (trackId);
            t->clips.push_back (c);
            sortClips (*t);
            return true;
        });
        ++nextClipId;
        return id;
    }

    bool Session::deleteClip (ClipId id)
    {
        return edit (Change::clips, "Delete clip", [&] (Project&)
        {
            auto* t = trackWithClip (id);
            return t != nullptr && std::erase_if (t->clips, [&] (const Clip& c) { return c.id == id; }) > 0;
        });
    }

    void Session::moveClip (ClipId id, time::Tick newStart, TrackId toTrack)
    {
        auto* from = trackWithClip (id);
        if (from == nullptr || newStart < 0)
            return;
        if (toTrack == 0)
            toTrack = from->id;
        auto* to = findTrack (toTrack);
        if (to == nullptr || to->type != from->type)
            return;

        edit (Change::clips, "Move clip", [&] (Project&)
        {
            auto* src = trackWithClip (id);
            auto it = std::find_if (src->clips.begin(), src->clips.end(), [&] (const Clip& c) { return c.id == id; });
            if (it->start == newStart && src->id == toTrack)
                return false;

            Clip moved = *it;
            moved.start = newStart;
            src->clips.erase (it);
            auto* dst = track (toTrack);
            dst->clips.push_back (moved);
            sortClips (*src);
            sortClips (*dst);
            return true;
        });
    }

    void Session::resizeClip (ClipId id, time::Tick newLength)
    {
        if (newLength < 1 || newLength > Project::maxClipLength || findClip (id) == nullptr)
            return;
        edit (Change::clips, "Resize clip", [&] (Project&) { return assign (clip (id)->length, newLength); });
    }

    ClipId Session::duplicateClip (ClipId id)
    {
        if (findClip (id) == nullptr)
            return 0;

        const auto newId = nextClipId;
        edit (Change::clips, "Duplicate clip", [&] (Project&)
        {
            auto* t = trackWithClip (id);
            Clip copy = *clip (id);
            copy.id = newId;
            copy.start = copy.end();
            t->clips.push_back (copy);
            sortClips (*t);
            return true;
        });
        ++nextClipId;
        return newId;
    }

    ClipId Session::pasteClip (TrackId trackId, const Clip& content, TrackType contentType, time::Tick start)
    {
        const auto* t = findTrack (trackId);
        if (t == nullptr || t->type != contentType || start < 0)
            return 0;

        const auto newId = nextClipId;
        edit (Change::clips, "Paste clip", [&] (Project&)
        {
            Clip copy = content;
            copy.id = newId;
            copy.start = start;
            auto* target = track (trackId);
            target->clips.push_back (copy);
            sortClips (*target);
            return true;
        });
        ++nextClipId;
        refreshIds();   // pasted note ids must not be reused by later notes
        return newId;
    }

    //==============================================================================
    void Session::setStep (ClipId id, int lane, int step, bool on)
    {
        auto* t = trackWithClip (id);
        if (t == nullptr || t->type != TrackType::drums || ! validLane (lane) || step < 0 || step >= stepsPerPattern)
            return;
        edit (Change::clips, "Edit steps", [&] (Project&) { return assign (clip (id)->pattern[(size_t) lane][(size_t) step], on); });
    }

    void Session::clearPattern (ClipId id)
    {
        auto* t = trackWithClip (id);
        if (t == nullptr || t->type != TrackType::drums)
            return;
        edit (Change::clips, "Clear pattern", [&] (Project&) { return assign (clip (id)->pattern, DrumPattern {}); });
    }

    midi::NoteId Session::addNote (ClipId id, midi::Note note)
    {
        auto* t = trackWithClip (id);
        if (t == nullptr || t->type != TrackType::instrument)
            return 0;

        const auto noteId = nextNoteId;
        edit (Change::clips, "Add note", [&] (Project&)
        {
            auto* c = clip (id);
            note = midi::constrained (note, c->length);
            note.id = noteId;
            c->notes.push_back (note);
            return true;
        });
        ++nextNoteId;
        return noteId;
    }

    void Session::updateNotes (ClipId id, const std::vector<midi::Note>& updates)
    {
        if (findClip (id) == nullptr)
            return;
        edit (Change::clips, "Edit notes", [&] (Project&)
        {
            auto* c = clip (id);
            bool any = false;
            for (const auto& u : updates)
                for (auto& n : c->notes)
                    if (n.id == u.id)
                    {
                        const auto next = midi::constrained (u, c->length);
                        any |= ! (next == n);
                        n = next;
                    }
            return any;
        });
    }

    void Session::removeNotes (ClipId id, const std::vector<midi::NoteId>& ids)
    {
        if (findClip (id) == nullptr)
            return;
        edit (Change::clips, "Delete notes", [&] (Project&)
        {
            auto* c = clip (id);
            const auto removed = std::erase_if (c->notes, [&] (const midi::Note& n)
            {
                return std::find (ids.begin(), ids.end(), n.id) != ids.end();
            });
            for (auto n : ids)
                c->noteExtra.erase (n);
            return removed > 0;
        });
    }

    void Session::quantizeNotes (ClipId id, const std::vector<midi::NoteId>& ids, time::Tick grid)
    {
        if (findClip (id) == nullptr || ! midi::isValidGrid (grid))
            return;
        edit (Change::clips, "Quantize", [&] (Project&)
        {
            auto* c = clip (id);
            const auto before = c->notes;
            midi::quantize (c->notes, grid, c->length, ids);
            return c->notes != before;
        });
    }

    //==============================================================================
    void Session::beginGesture (std::string label)
    {
        if (gestureDepth++ == 0)
        {
            gestureRecorded = false;
            gestureLabel = std::move (label);
        }
    }

    void Session::endGesture()
    {
        if (gestureDepth > 0 && --gestureDepth == 0)
        {
            gestureRecorded = false;
            gestureLabel.clear();
        }
    }

    void Session::undo()
    {
        if (undoStack.empty())
            return;
        const bool wasDirty = isDirty(), couldUndo = canUndo(), couldRedo = canRedo();
        redoStack.push_back ({ project, undoStack.back().label });
        project = std::move (undoStack.back().project);
        undoStack.pop_back();
        refreshIds();
        syncEngine (Change::replaced);
        notify (Change::replaced, wasDirty, couldUndo, couldRedo);
    }

    void Session::redo()
    {
        if (redoStack.empty())
            return;
        const bool wasDirty = isDirty(), couldUndo = canUndo(), couldRedo = canRedo();
        undoStack.push_back ({ project, redoStack.back().label });
        project = std::move (redoStack.back().project);
        redoStack.pop_back();
        refreshIds();
        syncEngine (Change::replaced);
        notify (Change::replaced, wasDirty, couldUndo, couldRedo);
    }

    //==============================================================================
    void Session::newProject()
    {
        openProject (Project::makeDefault(), std::nullopt);
    }

    void Session::openProject (Project p, std::optional<std::filesystem::path> f)
    {
        const bool wasDirty = isDirty(), couldUndo = canUndo(), couldRedo = canRedo();
        replaceProject (std::move (p));
        savedProject = project;
        file = std::move (f);
        undoStack.clear();
        redoStack.clear();
        notify (Change::replaced, wasDirty, couldUndo, couldRedo);
    }

    void Session::markUnsaved()
    {
        const bool wasDirty = isDirty(), couldUndo = canUndo(), couldRedo = canRedo();
        savedProject = Project {};
        savedProject.name = "\x01 not saved";   // never equal to a real project
        notify (Change::metadata, wasDirty, couldUndo, couldRedo);
    }

    void Session::markSaved (const std::filesystem::path& f)
    {
        const bool wasDirty = isDirty(), couldUndo = canUndo(), couldRedo = canRedo();
        savedProject = project;
        file = f;
        notify (Change::metadata, wasDirty, couldUndo, couldRedo);
    }

    //==============================================================================
    int Session::addListener (Listener listener)
    {
        listeners.emplace_back (nextListenerId, std::move (listener));
        return nextListenerId++;
    }

    void Session::removeListener (int id)
    {
        std::erase_if (listeners, [id] (auto& l) { return l.first == id; });
    }
}
