#include "beatforge/api/CommandApi.h"
#include "beatforge/midi/Note.h"
#include "beatforge/project/MidiExport.h"
#include "beatforge/project/ProjectFile.h"
#include "beatforge/time/MusicalTime.h"
#include "beatforge/time/TempoMap.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace beatforge::api
{
    using project::Clip;
    using project::Track;
    using project::TrackType;
    using Error = CommandApi::Error;

    namespace
    {
        constexpr const char* laneKeys[] { "kick", "snare", "closed_hat", "open_hat", "clap" };
        constexpr int maxNotesPerCall = 1024;

        [[noreturn]] void fail (const std::string& code, const std::string& message) { throw Error { code, message }; }
        [[noreturn]] void invalid (const std::string& message) { fail ("invalid_argument", message); }

        const Json* find (const Json& args, const char* key)
        {
            const auto it = args.find (key);
            return it == args.end() || it->is_null() ? nullptr : &*it;
        }

        double number (const Json& args, const char* key, double lo, double hi)
        {
            const auto* v = find (args, key);
            if (v == nullptr) invalid (std::string ("'") + key + "' is required");
            if (! v->is_number()) invalid (std::string ("'") + key + "' must be a number");
            const auto d = v->get<double>();
            if (! std::isfinite (d) || d < lo || d > hi)
                invalid (std::string ("'") + key + "' must be between " + Json (lo).dump() + " and " + Json (hi).dump());
            return d;
        }

        double numberOr (const Json& args, const char* key, double lo, double hi, double fallback)
        {
            return find (args, key) == nullptr ? fallback : number (args, key, lo, hi);
        }

        int integer (const Json& args, const char* key, int lo, int hi)
        {
            const auto d = number (args, key, lo, hi);
            if (d != std::floor (d)) invalid (std::string ("'") + key + "' must be a whole number");
            return (int) d;
        }

        int integerOr (const Json& args, const char* key, int lo, int hi, int fallback)
        {
            return find (args, key) == nullptr ? fallback : integer (args, key, lo, hi);
        }

        std::string text (const Json& args, const char* key, size_t maxLength)
        {
            const auto* v = find (args, key);
            if (v == nullptr || ! v->is_string()) invalid (std::string ("'") + key + "' must be a string");
            auto s = v->get<std::string>();
            if (s.empty() || s.size() > maxLength)
                invalid (std::string ("'") + key + "' must be 1 to " + std::to_string (maxLength) + " characters");
            return s;
        }

        bool flag (const Json& args, const char* key, bool fallback = false)
        {
            const auto* v = find (args, key);
            if (v == nullptr) return fallback;
            if (! v->is_boolean()) invalid (std::string ("'") + key + "' must be true or false");
            return v->get<bool>();
        }

        std::string lower (std::string s)
        {
            std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            return s;
        }

        std::string toolInstrumentId (audio::InstrumentType t) { return t == audio::InstrumentType::monoBass ? "nebula_bass" : "nebula_poly"; }

        audio::InstrumentType instrumentArg (const Json& args, const char* key)
        {
            const auto id = lower (text (args, key, 32));
            if (id == "nebula_poly" || id == "poly" || id == "nebula poly") return audio::InstrumentType::polySynth;
            if (id == "nebula_bass" || id == "bass" || id == "nebula bass") return audio::InstrumentType::monoBass;
            invalid ("unknown instrument '" + id + "' (use nebula_poly or nebula_bass)");
        }

        bool within (const std::filesystem::path& p, const std::filesystem::path& root)
        {
            const auto rel = p.lexically_relative (root);
            return ! rel.empty() && (rel == "." || *rel.begin() != "..");
        }

        constexpr const char* waveNames[] { "sine", "saw", "square", "triangle" };
    }

    //==============================================================================
    int parseNoteName (const std::string& s) noexcept
    {
        if (s.empty()) return -1;
        static constexpr int base[] { 9, 11, 0, 2, 4, 5, 7 };   // A..G
        const auto letter = (char) std::toupper ((unsigned char) s[0]);
        if (letter < 'A' || letter > 'G') return -1;
        int pitch = base[letter - 'A'];
        size_t i = 1;
        if (i < s.size() && (s[i] == '#' || s[i] == 'b')) { pitch += s[i] == '#' ? 1 : -1; ++i; }
        if (i >= s.size()) return -1;
        int octave = 0;
        try
        {
            size_t used = 0;
            octave = std::stoi (s.substr (i), &used);
            if (i + used != s.size()) return -1;
        }
        catch (...) { return -1; }
        const auto n = (octave + 1) * 12 + pitch;   // C4 = 60
        return n >= 0 && n <= 127 ? n : -1;
    }

    std::string noteName (int pitch)
    {
        static constexpr const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return std::string (names[pitch % 12]) + std::to_string (pitch / 12 - 1);
    }

    const std::vector<std::string>& CommandApi::operations()
    {
        static const std::vector<std::string> ops {
            "hello", "get_project_state", "create_project", "save_project", "open_project", "undo", "redo",
            "get_transport_state", "get_tempo", "set_tempo", "set_time_signature", "set_loop", "set_metronome",
            "play", "stop", "seek",
            "list_tracks", "create_track", "rename_track", "delete_track", "set_track_volume", "set_track_pan",
            "set_track_mute", "set_track_solo", "set_master_volume",
            "create_clip", "delete_clip", "set_drum_pattern", "get_clip_notes", "add_midi_notes", "remove_midi_notes",
            "quantize_clip",
            "list_instruments", "set_instrument", "set_instrument_param", "set_instrument_preset",
            "export_wav", "get_export_status", "cancel_export", "export_midi" };
        return ops;
    }

    //==============================================================================
    struct CommandApi::ExportJob
    {
        std::string id;
        std::filesystem::path file;
        project::AudioExportOptions options;
        project::AudioExportProgress progress;
        project::AudioExportResult result;
        std::atomic<bool> done { false };
        std::thread thread;

        ~ExportJob()
        {
            progress.cancel = true;
            if (thread.joinable()) thread.join();
        }
    };

    CommandApi::CommandApi (project::Session& s, audio::AudioEngine& e, Environment environment)
        : session (s), engine (e), env (std::move (environment))
    {
        if (env.allowedFolders.empty())
            env.allowedFolders.push_back (std::filesystem::temp_directory_path() / "beatforge");
    }

    CommandApi::~CommandApi() = default;   // jobs cancel and join

    Json CommandApi::execute (const std::string& op, const Json& args)
    {
        try
        {
            if (! args.is_object() && ! args.is_null())
                invalid ("arguments must be an object");

            // Whatever a call changes is one undo step, named after it ("AI: add midi notes").
            struct Gesture
            {
                project::Session& s;
                Gesture (project::Session& session, const std::string& op) : s (session)
                {
                    auto words = op;
                    std::replace (words.begin(), words.end(), '_', ' ');
                    s.beginGesture ("AI: " + words);
                }
                ~Gesture() { s.endGesture(); }
            } gesture (session, op);

            return { { "ok", true }, { "result", run (op, args.is_null() ? Json::object() : args) } };
        }
        catch (const Error& e)
        {
            return { { "ok", false }, { "error", { { "code", e.code }, { "message", e.message } } } };
        }
        catch (const std::exception& e)
        {
            return { { "ok", false }, { "error", { { "code", "internal_error" }, { "message", e.what() } } } };
        }
    }

    //==============================================================================
    const Track& CommandApi::trackArg (const Json& args, const char* key) const
    {
        const auto* v = find (args, key);
        if (v == nullptr) invalid (std::string ("'") + key + "' is required (a track id or exact name)");
        const auto& tracks = session.getProject().tracks;
        if (v->is_number_integer())
        {
            if (const auto* t = session.findTrack ((project::TrackId) v->get<std::int64_t>()))
                return *t;
            fail ("not_found", "no track with id " + v->dump() + "; call list_tracks");
        }
        if (v->is_string())
        {
            for (auto& t : tracks)
                if (t.name == v->get<std::string>())
                    return t;
            fail ("not_found", "no track named " + v->dump() + "; call list_tracks");
        }
        invalid (std::string ("'") + key + "' must be a track id or name");
    }

    const Clip& CommandApi::clipArg (const Json& args, const char* key) const
    {
        const auto id = integer (args, key, 1, 1 << 30);
        if (const auto* c = session.findClip ((project::ClipId) id))
            return *c;
        fail ("not_found", "no clip with id " + std::to_string (id) + "; call get_project_state");
    }

    std::filesystem::path CommandApi::fileArg (const Json& args, const char* extension, bool forWriting) const
    {
        namespace fs = std::filesystem;
        fs::path p = text (args, "file", 1024);
        if (p.is_relative())
            p = env.allowedFolders.front() / p;
        if (! p.has_extension())
            p += extension;
        else if (lower (p.extension().string()) != extension)
            invalid ("the file must end in " + std::string (extension));

        std::error_code ec;
        if (forWriting)
            fs::create_directories (env.allowedFolders.front(), ec);   // the default folder may not exist yet
        const auto parent = fs::weakly_canonical (p.parent_path(), ec);
        auto target = parent / p.filename();
        if (fs::is_symlink (target, ec))
            target = fs::weakly_canonical (target, ec);   // a link must not lead outside

        const bool allowed = std::any_of (env.allowedFolders.begin(), env.allowedFolders.end(), [&] (const fs::path& root)
        {
            std::error_code e;
            const auto r = fs::weakly_canonical (root, e);
            return within (target.parent_path(), r);
        });
        if (! allowed)
        {
            std::string roots;
            for (auto& r : env.allowedFolders) roots += (roots.empty() ? "" : ", ") + r.string();
            fail ("path_not_allowed", "files can only be used inside: " + roots);
        }

        if (forWriting)
            fs::create_directories (target.parent_path(), ec);
        else if (! fs::exists (target))
            fail ("not_found", target.string() + " does not exist");
        return target;
    }

    void CommandApi::requireConfirm (const Json& args, const char* key, const std::string& why) const
    {
        if (! flag (args, key))
            fail ("confirmation_required", why + " Ask the user first, then call again with " + key + ": true.");
    }

    //==============================================================================
    Json CommandApi::clipJson (const Track& t, const Clip& c, bool withContent) const
    {
        const auto& p = session.getProject();
        const auto perBar = (double) p.timeSignature.ticksPerBar(), perBeat = (double) p.timeSignature.ticksPerBeat();
        Json j { { "id", c.id }, { "track", t.id }, { "start_bar", (double) c.start / perBar + 1.0 },
                 { "bars", (double) c.length / perBar }, { "length_beats", (double) c.length / perBeat } };

        if (t.type == TrackType::drums)
        {
            Json pattern = Json::object();
            for (int lane = 0; lane < 5; ++lane)
            {
                std::string steps;
                for (int s = 0; s < project::stepsPerPattern; ++s)
                    steps += c.pattern[(size_t) lane][(size_t) s] ? 'x' : '.';
                pattern[laneKeys[lane]] = steps;
            }
            j["pattern"] = pattern;
            j["pattern_note"] = "16 steps of 1/16, repeating every bar of the clip";
            return j;
        }

        j["note_count"] = c.notes.size();
        if (withContent)
        {
            auto notes = c.notes;
            std::sort (notes.begin(), notes.end(), [] (const midi::Note& a, const midi::Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
            Json list = Json::array();
            for (auto& n : notes)
                list.push_back ({ { "id", n.id }, { "pitch", n.pitch }, { "name", noteName (n.pitch) },
                                  { "start_beat", (double) n.start / perBeat }, { "length_beats", (double) n.length / perBeat },
                                  { "velocity", n.velocity }, { "beyond_clip_end", n.start >= c.length } });
            j["notes"] = list;
        }
        return j;
    }

    Json CommandApi::trackJson (const Track& t) const
    {
        Json j { { "id", t.id }, { "name", t.name }, { "type", t.type == TrackType::drums ? "drums" : "synth" },
                 { "volume_db", t.volumeDb }, { "pan", t.pan }, { "muted", t.muted }, { "soloed", t.soloed } };
        if (t.type == TrackType::instrument)
            j["instrument"] = toolInstrumentId (t.instrument);
        Json clips = Json::array();
        for (auto& c : t.clips)
            clips.push_back (clipJson (t, c, false));
        j["clips"] = clips;
        return j;
    }

    Json CommandApi::projectState() const
    {
        const auto& p = session.getProject();
        const auto perBar = p.timeSignature.ticksPerBar();
        Json tracks = Json::array();
        for (auto& t : p.tracks)
            tracks.push_back (trackJson (t));
        return { { "name", p.name },
                 { "file", session.getFile() ? Json (session.getFile()->string()) : Json (nullptr) },
                 { "unsaved_changes", session.isDirty() },
                 { "tempo_bpm", p.tempo },
                 { "time_signature", std::to_string (p.timeSignature.numerator) + "/" + std::to_string (p.timeSignature.denominator) },
                 { "song_bars", (double) project::songEnd (p) / (double) perBar },
                 { "loop", { { "enabled", p.loopEnabled }, { "start_bar", p.loopStart / perBar + 1 }, { "bars", (p.loopEnd - p.loopStart) / perBar } } },
                 { "metronome", p.metronomeEnabled },
                 { "master_volume_db", p.masterVolumeDb },
                 { "can_undo", session.canUndo() }, { "can_redo", session.canRedo() },
                 { "tracks", tracks } };
    }

    Json CommandApi::transportState() const
    {
        const auto& p = session.getProject();
        const auto ticks = engine.getPositionTicks();
        const auto bbt = time::toBarBeatTick ((time::Tick) ticks, p.timeSignature);
        return { { "state", engine.isPlayRequested() ? "playing" : "stopped" },
                 { "recording", engine.isRecording() },
                 { "position", { { "bar", bbt.bar }, { "beat", bbt.beat }, { "tick", bbt.tick },
                                 { "seconds", ticks * time::TempoMap::secondsPerTick (p.tempo) } } },
                 { "play_start_bar", engine.getPlayStart() / (double) p.timeSignature.ticksPerBar() + 1.0 },
                 { "tempo_bpm", p.tempo },
                 { "loop_enabled", p.loopEnabled }, { "metronome", p.metronomeEnabled },
                 { "audio_device", env.audioAvailable ? env.audioAvailable() : true } };
    }

    //==============================================================================
    Json CommandApi::run (const std::string& op, const Json& a)
    {
        const auto& p = session.getProject();
        const auto perBar = p.timeSignature.ticksPerBar(), perBeat = p.timeSignature.ticksPerBeat();

        // --- project --------------------------------------------------------------
        if (op == "hello")
        {
            Json folders = Json::array();
            for (auto& f : env.allowedFolders) folders.push_back (f.string());
            return { { "app", "BeatForge" }, { "version", env.appVersion }, { "protocol", 1 },
                     { "operations", operations() }, { "allowed_folders", folders } };
        }
        if (op == "get_project_state")
            return projectState();

        if (op == "create_project")
        {
            if (session.isDirty())
                requireConfirm (a, "confirm", "The open project \"" + p.name + "\" has unsaved changes that would be lost.");
            session.newProject();
            return projectState();
        }

        if (op == "save_project")
        {
            std::filesystem::path file;
            if (find (a, "file") != nullptr)
            {
                file = fileArg (a, project::fileExtension, true);
                if (std::filesystem::exists (file) && (! session.getFile() || *session.getFile() != file))
                    requireConfirm (a, "overwrite", file.string() + " already exists and would be replaced.");
            }
            else if (session.getFile())
                file = *session.getFile();
            else
                fail ("file_required", "the project has never been saved; give a file name (e.g. \"My Song\")");

            if (p.name == project::Project {}.name)   // like Save As: an untitled project takes the file's name
                session.renameProject (file.stem().string().substr (0, (size_t) project::Project::maxNameLength));
            const auto saved = project::saveFile (session.getProject(), file, env.appVersion);
            if (! saved.ok())
                fail ("save_failed", saved.error);
            session.markSaved (file);
            if (env.fileSaved) env.fileSaved (file);
            return { { "file", file.string() }, { "name", session.getProject().name } };
        }

        if (op == "open_project")
        {
            const auto file = fileArg (a, project::fileExtension, false);
            if (session.isDirty())
                requireConfirm (a, "confirm", "The open project \"" + p.name + "\" has unsaved changes that would be lost.");
            auto loaded = project::loadFile (file);
            if (! loaded.project)
                fail ("open_failed", loaded.error);
            session.openProject (std::move (*loaded.project), file);
            auto state = projectState();
            state["warnings"] = loaded.warnings;
            return state;
        }

        if (op == "undo" || op == "redo")
        {
            if (op == "undo" ? ! session.canUndo() : ! session.canRedo())
                fail ("nothing_to_" + op, "there is nothing to " + op);
            op == "undo" ? session.undo() : session.redo();
            return { { "can_undo", session.canUndo() }, { "can_redo", session.canRedo() } };
        }

        // --- transport ------------------------------------------------------------
        if (op == "get_transport_state") return transportState();
        if (op == "get_tempo")           return { { "bpm", p.tempo } };

        if (op == "set_tempo")
        {
            const auto before = p.tempo;
            session.setTempo (number (a, "bpm", time::minTempo, time::maxTempo));
            return { { "previous_bpm", before }, { "bpm", session.getProject().tempo } };
        }

        if (op == "set_time_signature")
        {
            const time::TimeSignature sig { integer (a, "numerator", 1, 16), integer (a, "denominator", 1, 16) };
            if (! sig.isValid())
                invalid ("unsupported time signature");
            session.setTimeSignature (sig);
            return { { "time_signature", std::to_string (sig.numerator) + "/" + std::to_string (sig.denominator) } };
        }

        if (op == "set_loop")
        {
            const auto start = (time::Tick) (integerOr (a, "start_bar", 1, 4096, (int) (p.loopStart / perBar) + 1) - 1) * perBar;
            const auto bars = integerOr (a, "bars", 1, 256, (int) ((p.loopEnd - p.loopStart) / perBar));
            session.setLoop (flag (a, "enabled", p.loopEnabled), start, start + bars * perBar);
            const auto& q = session.getProject();
            return { { "enabled", q.loopEnabled }, { "start_bar", q.loopStart / perBar + 1 }, { "bars", (q.loopEnd - q.loopStart) / perBar } };
        }

        if (op == "set_metronome")
        {
            session.setMetronomeEnabled (flag (a, "enabled"));
            return { { "metronome", session.getProject().metronomeEnabled } };
        }

        if (op == "play")
        {
            if (env.audioAvailable && ! env.audioAvailable())
                fail ("no_audio_device", "no audio output device is open; the user must choose one in Audio Settings");
            engine.play();
            return { { "state", "playing" }, { "from_bar", engine.getPlayStart() / (double) perBar + 1.0 } };
        }

        if (op == "stop")
        {
            engine.setRecording (false);
            engine.stop();
            return { { "state", "stopped" }, { "returned_to_bar", engine.getPlayStart() / (double) perBar + 1.0 } };
        }

        if (op == "seek")
        {
            const auto bar = number (a, "bar", 1.0, 4096.0);
            const auto beat = numberOr (a, "beat", 1.0, (double) p.timeSignature.numerator + 0.999, 1.0);
            const auto tick = (bar - 1.0) * (double) perBar + (beat - 1.0) * (double) perBeat;
            engine.seek (tick);
            return { { "position_bar", bar }, { "position_beat", beat } };
        }

        // --- tracks -----------------------------------------------------------------
        if (op == "list_tracks")
        {
            Json list = Json::array();
            for (auto& t : p.tracks) list.push_back (trackJson (t));
            return list;
        }

        if (op == "create_track")
        {
            const auto type = lower (text (a, "type", 16));
            if (type != "drums" && type != "synth")
                invalid ("'type' must be \"drums\" or \"synth\"");
            const auto name = find (a, "name") != nullptr ? text (a, "name", (size_t) project::Project::maxNameLength) : std::string();
            const auto instrument = type == "synth" && find (a, "instrument") != nullptr ? instrumentArg (a, "instrument")
                                                                                           : audio::InstrumentType::polySynth;
            session.beginGesture();   // one undo step
            const auto id = session.addTrack (type == "drums" ? TrackType::drums : TrackType::instrument);
            if (id != 0)
            {
                if (! name.empty()) session.renameTrack (id, name);
                if (type == "synth") session.setInstrument (id, instrument);
            }
            session.endGesture();
            if (id == 0)
                fail ("limit_reached", "a project can have at most " + std::to_string (project::Project::maxTracks) + " tracks");
            if (env.reveal) env.reveal (id, 0);
            return trackJson (*session.findTrack (id));
        }

        if (op == "rename_track")
        {
            const auto& t = trackArg (a);
            const auto id = t.id;
            if (! session.renameTrack (id, text (a, "name", (size_t) project::Project::maxNameLength)))
                invalid ("invalid name");
            return trackJson (*session.findTrack (id));
        }

        if (op == "delete_track")
        {
            const auto& t = trackArg (a);
            requireConfirm (a, "confirm", "Deleting track \"" + t.name + "\" removes it and its " + std::to_string (t.clips.size())
                                               + " clip(s) (undo can bring it back).");
            const auto name = t.name;
            session.removeTrack (t.id);
            return { { "deleted", name } };
        }

        if (op == "set_track_volume" || op == "set_track_pan" || op == "set_track_mute" || op == "set_track_solo")
        {
            const auto id = trackArg (a).id;
            if (op == "set_track_volume") session.setTrackVolume (id, (float) number (a, "db", project::Project::minVolumeDb, project::Project::maxVolumeDb));
            if (op == "set_track_pan")    session.setTrackPan (id, (float) number (a, "pan", -1.0, 1.0));
            if (op == "set_track_mute")   session.setTrackMuted (id, flag (a, "muted", true));
            if (op == "set_track_solo")   session.setTrackSoloed (id, flag (a, "soloed", true));
            return trackJson (*session.findTrack (id));
        }

        if (op == "set_master_volume")
        {
            session.setMasterVolume ((float) number (a, "db", project::Project::minVolumeDb, project::Project::maxVolumeDb));
            return { { "master_volume_db", session.getProject().masterVolumeDb } };
        }

        // --- clips ------------------------------------------------------------------
        if (op == "create_clip")
        {
            const auto& t = trackArg (a);
            const auto trackId = t.id;
            const auto bar = integer (a, "bar", 1, 4096);
            const auto bars = number (a, "bars", 1.0 / 16.0, 64.0);   // 1/16 bar steps are fine
            const auto length = (time::Tick) std::llround (bars * (double) perBar);
            const auto id = session.createClip (trackId, (time::Tick) (bar - 1) * perBar, length);
            if (id == 0)
                invalid ("could not create the clip (length up to 64 bars)");
            if (env.reveal) env.reveal (trackId, id);
            return clipJson (*session.findTrack (trackId), *session.findClip (id), true);
        }

        if (op == "delete_clip")
        {
            const auto& c = clipArg (a);
            requireConfirm (a, "confirm", "Deleting clip " + std::to_string (c.id) + " removes its content (undo can bring it back).");
            session.deleteClip (c.id);
            return { { "deleted_clip", integer (a, "clip", 1, 1 << 30) } };
        }

        if (op == "set_drum_pattern")
        {
            const auto& c = clipArg (a);
            const auto clipId = c.id;
            const auto& t = *session.findTrack (session.trackOfClip (clipId));
            if (t.type != TrackType::drums)
                invalid ("clip " + std::to_string (clipId) + " is on a synth track; use add_midi_notes");
            const auto* pattern = find (a, "pattern");
            if (pattern == nullptr || ! pattern->is_object() || pattern->empty())
                invalid ("'pattern' must be an object like {\"kick\": \"x...x...x...x...\"} (lanes: kick, snare, closed_hat, open_hat, clap)");

            std::array<std::string, 5> rows;
            for (auto& [key, value] : pattern->items())
            {
                const auto lane = (int) (std::find (std::begin (laneKeys), std::end (laneKeys), key) - std::begin (laneKeys));
                if (lane >= 5) invalid ("unknown drum lane '" + key + "' (use kick, snare, closed_hat, open_hat, clap)");
                if (! value.is_string() || value.get<std::string>().size() != (size_t) project::stepsPerPattern)
                    invalid ("lane '" + key + "' must be a string of 16 steps, 'x' = hit, '.' = rest");
                for (char ch : value.get<std::string>())
                    if (ch != 'x' && ch != 'X' && ch != '.' && ch != '-')
                        invalid ("lane '" + key + "' may only contain 'x' and '.'");
                rows[(size_t) lane] = value.get<std::string>();
            }

            session.beginGesture();
            for (int lane = 0; lane < 5; ++lane)
                for (int s = 0; s < (int) rows[(size_t) lane].size(); ++s)
                    session.setStep (clipId, lane, s, rows[(size_t) lane][(size_t) s] == 'x' || rows[(size_t) lane][(size_t) s] == 'X');
            session.endGesture();
            return clipJson (t, *session.findClip (clipId), true);
        }

        if (op == "get_clip_notes")
        {
            const auto& c = clipArg (a);
            return clipJson (*session.findTrack (session.trackOfClip (c.id)), c, true);
        }

        if (op == "add_midi_notes")
        {
            const auto& c = clipArg (a);
            const auto clipId = c.id;
            const auto clipLength = c.length;
            if (session.findTrack (session.trackOfClip (clipId))->type != TrackType::instrument)
                invalid ("clip " + std::to_string (clipId) + " is on a drum track; use set_drum_pattern");
            const auto* notes = find (a, "notes");
            if (notes == nullptr || ! notes->is_array() || notes->empty() || notes->size() > (size_t) maxNotesPerCall)
                invalid ("'notes' must be a list of 1 to " + std::to_string (maxNotesPerCall) + " notes");

            // Validate everything first: either all notes are added or none.
            std::vector<midi::Note> parsed;
            for (size_t i = 0; i < notes->size(); ++i)
            {
                const auto& n = (*notes)[i];
                const auto where = "note " + std::to_string (i + 1) + ": ";
                if (! n.is_object()) invalid (where + "must be an object");
                midi::Note note;
                const auto* pitch = find (n, "pitch");
                if (pitch != nullptr && pitch->is_string())
                    note.pitch = parseNoteName (pitch->get<std::string>());
                else if (pitch != nullptr && pitch->is_number_integer())
                    note.pitch = pitch->get<int>();
                else
                    note.pitch = -1;
                if (note.pitch < 0 || note.pitch > 127)
                    invalid (where + "'pitch' must be 0-127 or a name like \"C4\" (C4 = 60)");
                try
                {
                    const auto start = number (n, "start_beat", 0.0, 1.0e6);
                    const auto length = number (n, "length_beats", 1.0 / 64.0, 1.0e6);
                    note.velocity = integerOr (n, "velocity", 1, 127, 100);
                    note.start = (time::Tick) std::llround (start * (double) perBeat);
                    note.length = std::max<time::Tick> (1, (time::Tick) std::llround (length * (double) perBeat));
                }
                catch (const Error& e) { invalid (where + e.message); }
                if (note.start >= clipLength)
                    invalid (where + "starts at beat " + Json ((double) note.start / (double) perBeat).dump() + ", but the clip is only "
                             + Json ((double) clipLength / (double) perBeat).dump() + " beats long");
                parsed.push_back (note);
            }

            Json ids = Json::array();
            int cut = 0;
            session.beginGesture();   // one undo step for the whole call
            for (auto& note : parsed)
            {
                if (note.end() > clipLength) ++cut;
                ids.push_back (session.addNote (clipId, note));
            }
            session.endGesture();
            if (env.reveal) env.reveal (session.trackOfClip (clipId), clipId);
            auto result = clipJson (*session.findTrack (session.trackOfClip (clipId)), *session.findClip (clipId), true);
            result["added_note_ids"] = ids;
            if (cut > 0) result["warning"] = std::to_string (cut) + " note(s) run past the clip end and are cut there";
            return result;
        }

        if (op == "remove_midi_notes")
        {
            const auto& c = clipArg (a);
            const auto clipId = c.id;
            std::vector<midi::NoteId> ids;
            if (flag (a, "all"))
            {
                for (auto& n : c.notes) ids.push_back (n.id);
            }
            else
            {
                const auto* list = find (a, "note_ids");
                if (list == nullptr || ! list->is_array() || list->empty())
                    invalid ("give 'note_ids' (from get_clip_notes) or all: true");
                for (auto& v : *list)
                {
                    if (! v.is_number_integer() || session.findNote (clipId, (midi::NoteId) v.get<std::int64_t>()) == nullptr)
                        fail ("not_found", "clip " + std::to_string (clipId) + " has no note " + v.dump());
                    ids.push_back ((midi::NoteId) v.get<std::int64_t>());
                }
            }
            requireConfirm (a, "confirm", "Removing " + std::to_string (ids.size()) + " note(s) from clip " + std::to_string (clipId) + " (undo can bring them back).");
            session.removeNotes (clipId, ids);
            auto result = clipJson (*session.findTrack (session.trackOfClip (clipId)), *session.findClip (clipId), true);
            result["removed"] = ids.size();
            return result;
        }

        if (op == "quantize_clip")
        {
            const auto& c = clipArg (a);
            const auto clipId = c.id;
            const auto grid = text (a, "grid", 8);
            const std::map<std::string, time::Tick> grids { { "1/4", 960 }, { "1/8", 480 }, { "1/16", 240 }, { "1/32", 120 } };
            if (grids.count (grid) == 0) invalid ("'grid' must be 1/4, 1/8, 1/16 or 1/32");
            session.quantizeNotes (clipId, {}, grids.at (grid));
            return clipJson (*session.findTrack (session.trackOfClip (clipId)), *session.findClip (clipId), true);
        }

        // --- instruments --------------------------------------------------------------
        if (op == "list_instruments")
        {
            Json list = Json::array();
            for (auto type : { audio::InstrumentType::polySynth, audio::InstrumentType::monoBass })
            {
                Json params = Json::array();
                for (int i = 0; i < audio::numParams; ++i)
                {
                    const auto param = (audio::Param) i;
                    if (! audio::usesParam (type, param)) continue;
                    const auto& spec = audio::paramSpec (param);
                    Json j { { "id", std::string (spec.id) }, { "label", std::string (spec.label) }, { "unit", std::string (spec.unit) },
                             { "min", spec.min }, { "max", spec.max }, { "default", audio::defaultValue (type, param) } };
                    if (param == audio::Param::wave) j["values"] = { "sine", "saw", "square", "triangle" };
                    params.push_back (j);
                }
                Json presets = Json::array();
                for (auto& preset : audio::presetsFor (type))
                    presets.push_back ({ { "id", std::string (preset.id) }, { "name", std::string (preset.name) } });
                list.push_back ({ { "id", toolInstrumentId (type) }, { "name", std::string (audio::instrumentName (type)) },
                                  { "presets", presets },
                                  { "voices", type == audio::InstrumentType::monoBass ? "mono (last note wins, glide)" : "16-voice polyphonic" },
                                  { "params", params } });
            }
            return list;
        }

        if (op == "set_instrument")
        {
            const auto& t = trackArg (a);
            const auto id = t.id;
            if (t.type != TrackType::instrument) invalid ("track \"" + t.name + "\" is a drum track");
            session.setInstrument (id, instrumentArg (a, "instrument"));
            return trackJson (*session.findTrack (id));
        }

        if (op == "set_instrument_preset")
        {
            const auto& t = trackArg (a);
            const auto id = t.id;
            if (t.type != TrackType::instrument) invalid ("track \"" + t.name + "\" is a drum track");
            const auto type = find (a, "instrument") != nullptr ? instrumentArg (a, "instrument") : t.instrument;
            const auto presetId = lower (text (a, "preset", 32));
            const auto* preset = audio::findPreset (type, presetId);
            if (preset == nullptr)
            {
                std::string names;
                for (auto& p2 : audio::presetsFor (type)) names += (names.empty() ? "" : ", ") + std::string (p2.id);
                invalid ("unknown preset '" + presetId + "' for " + std::string (audio::instrumentName (type)) + " (" + names + ")");
            }
            session.applyPreset (id, *preset);
            auto j = trackJson (*session.findTrack (id));
            j["preset"] = std::string (preset->name);
            return j;
        }

        if (op == "set_instrument_param")
        {
            const auto& t = trackArg (a);
            const auto id = t.id;
            if (t.type != TrackType::instrument) invalid ("track \"" + t.name + "\" is a drum track");
            const auto key = text (a, "param", 32);
            int index = -1;
            for (int i = 0; i < audio::numParams; ++i)
                if (audio::paramSpec ((audio::Param) i).id == key) index = i;
            if (index < 0 || ! audio::usesParam (t.instrument, (audio::Param) index))
                invalid ("'" + key + "' is not a parameter of " + std::string (audio::instrumentName (t.instrument)) + "; see list_instruments");
            const auto param = (audio::Param) index;
            const auto& spec = audio::paramSpec (param);
            float value;
            if (param == audio::Param::wave && find (a, "value") != nullptr && find (a, "value")->is_string())
            {
                const auto name = lower (find (a, "value")->get<std::string>());
                const auto it = std::find (std::begin (waveNames), std::end (waveNames), name);
                if (it == std::end (waveNames)) invalid ("wave must be sine, saw, square or triangle");
                value = (float) (it - std::begin (waveNames));
            }
            else
            {
                value = (float) number (a, "value", spec.min, spec.max);
            }
            session.setInstrumentParam (id, t.instrument, param, value);
            const auto now = session.getInstrumentParam (id, session.findTrack (id)->instrument, param);
            return { { "track", id }, { "param", key }, { "value", now }, { "unit", std::string (spec.unit) } };
        }

        // --- export -------------------------------------------------------------------
        if (op == "export_wav")
        {
            project::AudioExportOptions o;
            const auto range = find (a, "range") != nullptr ? lower (text (a, "range", 8)) : std::string ("song");
            if (range != "song" && range != "loop") invalid ("'range' must be \"song\" or \"loop\"");
            o.range = range == "loop" ? project::AudioExportOptions::Range::loop : project::AudioExportOptions::Range::song;
            const auto rate = integerOr (a, "sample_rate", 8000, 192000, 48000);
            if (rate != 44100 && rate != 48000 && rate != 96000) invalid ("'sample_rate' must be 44100, 48000 or 96000");
            o.sampleRate = rate;
            const auto bits = integerOr (a, "bit_depth", 16, 32, 24);
            if (bits != 16 && bits != 24 && bits != 32) invalid ("'bit_depth' must be 16, 24 or 32 (float)");
            o.format = bits == 16 ? audio::SampleFormat::int16 : bits == 32 ? audio::SampleFormat::float32 : audio::SampleFormat::int24;

            const auto file = fileArg (a, ".wav", true);
            if (std::filesystem::exists (file))
                requireConfirm (a, "overwrite", file.string() + " already exists and would be replaced.");
            for (auto& [id, job] : jobs)
                if (! job->done && job->file == file)
                    fail ("busy", "export " + id + " is already writing " + file.string());
            if (o.range == project::AudioExportOptions::Range::song && project::songEnd (p) <= 0)
                fail ("nothing_to_export", "the song has no clips yet");

            auto job = std::make_unique<ExportJob>();
            job->id = "export-" + std::to_string (nextJob++);
            job->file = file;
            job->options = o;
            auto* j = job.get();
            j->thread = std::thread ([j, snapshot = p]
            {
                j->result = project::exportAudio (snapshot, j->file, j->options, &j->progress);
                j->done = true;
            });
            const auto id = job->id;
            jobs[id] = std::move (job);
            return { { "job", id }, { "file", file.string() }, { "state", "running" },
                     { "hint", "poll get_export_status with this job id" } };
        }

        if (op == "get_export_status" || op == "cancel_export")
        {
            const auto id = text (a, "job", 32);
            const auto it = jobs.find (id);
            if (it == jobs.end()) fail ("not_found", "no export job " + id);
            auto& job = *it->second;
            if (op == "cancel_export" && ! job.done)
                job.progress.cancel = true;
            if (job.done && job.thread.joinable())
                job.thread.join();

            Json j { { "job", id }, { "file", job.file.string() } };
            if (! job.done)
            {
                j["state"] = job.progress.cancel ? "cancelling" : "running";
                j["progress"] = job.progress.fraction.load();
                return j;
            }
            const auto& r = job.result;
            j["state"] = r.cancelled ? "cancelled" : r.ok() ? "done" : "failed";
            j["progress"] = r.ok() ? 1.0 : (double) job.progress.fraction.load();
            if (! r.error.empty()) j["error"] = r.error;
            if (r.ok())
            {
                j["seconds"] = r.seconds();
                j["music_seconds"] = r.musicSeconds();
                j["sample_rate"] = (int) r.sampleRate;
                j["bit_depth"] = audio::bitsOf (job.options.format);
                j["format"] = job.options.format == audio::SampleFormat::float32 ? "float" : "pcm";
                j["peak_dbfs"] = r.peak > 0.0f ? 20.0 * std::log10 (r.peak) : -144.0;
                j["samples_over_full_scale"] = r.samplesOverFullScale;
                j["validated"] = true;   // exportAudio read the file back
            }
            return j;
        }

        if (op == "export_midi")
        {
            const auto file = fileArg (a, ".mid", true);
            if (std::filesystem::exists (file))
                requireConfirm (a, "overwrite", file.string() + " already exists and would be replaced.");
            const auto error = project::saveMidiFile (p, file);
            if (! error.empty()) fail ("export_failed", error);
            const auto e = project::exportMidi (p);
            return { { "file", file.string() }, { "tracks", e.tracksWritten }, { "notes", e.notes },
                     { "bars", (double) e.length / (double) perBar } };
        }

        fail ("unknown_operation", "unknown operation '" + op + "'");
    }
}
