#include "EngineHelpers.h"
#include <beatforge/util/Paths.h>

#include <beatforge/api/CommandApi.h>
#include <beatforge/audio/WavFile.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace beatforge;
using api::CommandApi;
using api::Json;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    struct Rig
    {
        fs::path root = fs::temp_directory_path() / "beatforge-api-test";
        audio::AudioEngine engine;
        project::Session session { engine };
        bool device = true;
        project::TrackId revealedTrack = 0;
        project::ClipId revealedClip = 0;
        std::unique_ptr<CommandApi> api;

        Rig()
        {
            fs::remove_all (root);
            fs::create_directories (root);
            engine.prepare (48000.0);
            CommandApi::Environment env;
            env.allowedFolders = { root / "Music" };
            env.appVersion = "test";
            env.audioAvailable = [this] { return device; };
            env.reveal = [this] (project::TrackId t, project::ClipId c) { revealedTrack = t; revealedClip = c; };
            api = std::make_unique<CommandApi> (session, engine, env);
        }
        ~Rig() { api.reset(); fs::remove_all (root); }

        Json ok (const std::string& op, const Json& args = Json::object())
        {
            const auto r = api->execute (op, args);
            INFO (op << " " << args.dump() << " -> " << r.dump());
            REQUIRE (r["ok"] == true);
            return r["result"];
        }

        std::string error (const std::string& op, const Json& args = Json::object())
        {
            const auto r = api->execute (op, args);
            INFO (op << " " << args.dump() << " -> " << r.dump());
            REQUIRE (r["ok"] == false);
            REQUIRE (r["error"]["message"].get<std::string>().size() > 5);   // a real explanation
            return r["error"]["code"];
        }

        Json waitForExport (const std::string& job)
        {
            for (int i = 0; i < 600; ++i)
            {
                const auto s = ok ("get_export_status", { { "job", job } });
                if (s["state"] != "running" && s["state"] != "cancelling")
                    return s;
                std::this_thread::sleep_for (std::chrono::milliseconds (20));
            }
            FAIL ("export did not finish");
            return {};
        }
    };
}

TEST_CASE ("Phase 10 scenario: inspect, tempo, track, clip, notes, play, export, report", "[api]")
{
    Rig rig;

    // 1. Read the project.
    const auto state = rig.ok ("get_project_state");
    REQUIRE (state["tempo_bpm"] == 120.0);
    REQUIRE (state["tracks"].size() == 2);
    REQUIRE (state["tracks"][0]["type"] == "drums");

    // 2. Tempo.
    REQUIRE (rig.ok ("set_tempo", { { "bpm", 128 } })["previous_bpm"] == 120.0);
    REQUIRE (rig.session.getProject().tempo == 128.0);

    // 3. Track (real project, revealed in the UI).
    const auto track = rig.ok ("create_track", { { "type", "synth" }, { "name", "Lead" }, { "instrument", "nebula_poly" } });
    const auto trackId = track["id"].get<project::TrackId>();
    REQUIRE (rig.session.findTrack (trackId)->name == "Lead");
    REQUIRE (rig.revealedTrack == trackId);

    // 4. Clip: bars 1-2.
    const auto clip = rig.ok ("create_clip", { { "track", "Lead" }, { "bar", 1 }, { "bars", 2 } });
    const auto clipId = clip["id"].get<project::ClipId>();
    REQUIRE (rig.session.findClip (clipId)->length == 2 * 3840);

    // 5. Notes, by name and number; beats from the clip start.
    const auto added = rig.ok ("add_midi_notes", { { "clip", clipId }, { "notes", {
        { { "pitch", "C4" }, { "start_beat", 0 }, { "length_beats", 1 }, { "velocity", 110 } },
        { { "pitch", 64 }, { "start_beat", 1 }, { "length_beats", 1 } },
        { { "pitch", "G4" }, { "start_beat", 2.5 }, { "length_beats", 0.5 } } } } });
    REQUIRE (added["added_note_ids"].size() == 3);
    const auto& notes = rig.session.findClip (clipId)->notes;
    REQUIRE (notes.size() == 3);
    REQUIRE (notes[0].pitch == 60);
    REQUIRE (notes[0].velocity == 110);
    REQUIRE (notes[2].start == 2400);
    REQUIRE (notes[2].length == 480);
    REQUIRE (added["notes"][1]["name"] == "E4");

    // 6. Play: the engine plays the new notes.
    REQUIRE (rig.ok ("play")["state"] == "playing");
    const auto out = test::render (rig.engine, 24000);
    REQUIRE (test::peakIn (out) > 0.05f);
    REQUIRE (rig.ok ("get_transport_state")["state"] == "playing");
    rig.ok ("stop");

    // 7-8. Export and report.
    const auto job = rig.ok ("export_wav", { { "file", "scenario" }, { "sample_rate", 44100 }, { "bit_depth", 16 } });
    REQUIRE (job["file"].get<std::string>().find ("scenario.wav") != std::string::npos);
    const auto done = rig.waitForExport (job["job"]);
    REQUIRE (done["state"] == "done");
    REQUIRE (done["validated"] == true);
    REQUIRE (done["sample_rate"] == 44100);
    REQUIRE (done["bit_depth"] == 16);
    REQUIRE (done["music_seconds"].get<double>() == Approx (2 * 4 * 60.0 / 128.0).margin (1e-3));
    REQUIRE (done["peak_dbfs"].get<double>() > -40.0);

    audio::WavReader reader;
    std::string error;
    REQUIRE (reader.open (rig.root / "Music" / "scenario.wav", error));
    REQUIRE (reader.info().sampleRate == 44100);
}

TEST_CASE ("Arguments are validated before anything changes", "[api]")
{
    Rig rig;
    const auto before = rig.session.getProject();

    REQUIRE (rig.error ("make_coffee") == "unknown_operation");
    REQUIRE (rig.error ("set_tempo") == "invalid_argument");
    REQUIRE (rig.error ("set_tempo", { { "bpm", "fast" } }) == "invalid_argument");
    REQUIRE (rig.error ("set_tempo", { { "bpm", 500 } }) == "invalid_argument");
    REQUIRE (rig.error ("create_track", { { "type", "guitar" } }) == "invalid_argument");
    REQUIRE (rig.error ("rename_track", { { "track", 999 }, { "name", "x" } }) == "not_found");
    REQUIRE (rig.error ("create_clip", { { "track", "Nope" }, { "bar", 1 }, { "bars", 1 } }) == "not_found");
    REQUIRE (rig.error ("create_clip", { { "track", 2 }, { "bar", 1.5 }, { "bars", 1 } }) == "invalid_argument");
    REQUIRE (rig.error ("set_instrument", { { "track", 1 }, { "instrument", "nebula_poly" } }) == "invalid_argument");   // drums
    REQUIRE (rig.error ("set_instrument_param", { { "track", 2 }, { "param", "glide" }, { "value", 0.1 } }) == "invalid_argument");   // Poly has no glide
    REQUIRE (rig.api->execute ("set_tempo", Json::array())["error"]["code"] == "invalid_argument");
    REQUIRE (rig.session.getProject() == before);

    // Notes: all or nothing.
    const auto clip = rig.ok ("create_clip", { { "track", 2 }, { "bar", 1 }, { "bars", 1 } })["id"];
    REQUIRE (rig.error ("add_midi_notes", { { "clip", clip }, { "notes", {
        { { "pitch", "C4" }, { "start_beat", 0 }, { "length_beats", 1 } },
        { { "pitch", "H9" }, { "start_beat", 1 }, { "length_beats", 1 } } } } }) == "invalid_argument");
    REQUIRE (rig.error ("add_midi_notes", { { "clip", clip }, { "notes", {
        { { "pitch", 60 }, { "start_beat", 4 }, { "length_beats", 1 } } } } }) == "invalid_argument");   // past the 4-beat clip
    REQUIRE (rig.session.findClip (clip.get<project::ClipId>())->notes.empty());
    REQUIRE (rig.error ("set_drum_pattern", { { "clip", clip }, { "pattern", { { "kick", "x...x...x...x..." } } } }) == "invalid_argument");

    const auto drumClip = rig.session.getProject().tracks[0].clips[0].id;
    REQUIRE (rig.error ("set_drum_pattern", { { "clip", drumClip }, { "pattern", { { "kick", "x..x" } } } }) == "invalid_argument");
    REQUIRE (rig.error ("set_drum_pattern", { { "clip", drumClip }, { "pattern", { { "cowbell", "x...x...x...x..." } } } }) == "invalid_argument");
    rig.device = false;
    REQUIRE (rig.error ("play") == "no_audio_device");
}

TEST_CASE ("Files stay inside the allowed folders", "[api][safety]")
{
    Rig rig;
    rig.session.addTrack (project::TrackType::instrument);
    rig.ok ("create_clip", { { "track", 2 }, { "bar", 1 }, { "bars", 1 } });

    REQUIRE (rig.error ("export_midi", { { "file", "/etc/passwd" } }) == "path_not_allowed");
    REQUIRE (rig.error ("export_midi", { { "file", "notes.txt" } }) == "invalid_argument");      // wrong extension
    REQUIRE (rig.error ("export_midi", { { "file", "/tmp/elsewhere.mid" } }) == "path_not_allowed");
    REQUIRE (rig.error ("export_midi", { { "file", "../escape.mid" } }) == "path_not_allowed");
    REQUIRE (rig.error ("export_wav", { { "file", "sub/../../escape.wav" } }) == "path_not_allowed");
    REQUIRE (rig.error ("open_project", { { "file", "/etc/hosts.beatforge" } }) == "path_not_allowed");

    // A symlink inside pointing outside does not count as inside.
    fs::create_directories (rig.root / "Music");
    fs::create_directories (rig.root / "outside");
    fs::create_symlink (rig.root / "outside", rig.root / "Music" / "link");
    REQUIRE (rig.error ("export_midi", { { "file", "link/x.mid" } }) == "path_not_allowed");

    const auto r = rig.ok ("export_midi", { { "file", "songs/demo" } });   // subfolders are fine, extension added
    // The app reports the canonical path (on Windows the temp folder may be a short 8.3 name).
    REQUIRE (r["file"] == pathToUtf8 (fs::weakly_canonical (rig.root / "Music" / "songs") / "demo.mid"));
    REQUIRE (fs::exists (rig.root / "Music" / "songs" / "demo.mid"));
    REQUIRE_FALSE (fs::exists (rig.root / "escape.mid"));
}

TEST_CASE ("Destructive operations need confirmation", "[api][safety]")
{
    Rig rig;
    rig.ok ("set_tempo", { { "bpm", 100 } });   // unsaved changes now

    REQUIRE (rig.error ("create_project") == "confirmation_required");
    REQUIRE (rig.session.getProject().tempo == 100.0);
    REQUIRE (rig.error ("delete_track", { { "track", 1 } }) == "confirmation_required");
    REQUIRE (rig.session.getProject().tracks.size() == 2);

    rig.ok ("export_midi", { { "file", "a" } });
    REQUIRE (rig.error ("export_midi", { { "file", "a" } }) == "confirmation_required");   // would overwrite
    rig.ok ("export_midi", { { "file", "a" }, { "overwrite", true } });

    REQUIRE (rig.ok ("delete_track", { { "track", 1 }, { "confirm", true } })["deleted"] == "Drums");
    REQUIRE (rig.ok ("create_project", { { "confirm", true } })["tempo_bpm"] == 120.0);
}

TEST_CASE ("Each call is one undo step; save and open stay in the allowed folder", "[api]")
{
    Rig rig;
    const auto clip = rig.ok ("create_clip", { { "track", 2 }, { "bar", 1 }, { "bars", 1 } })["id"];
    rig.ok ("add_midi_notes", { { "clip", clip }, { "notes", {
        { { "pitch", 60 }, { "start_beat", 0 }, { "length_beats", 1 } },
        { { "pitch", 62 }, { "start_beat", 1 }, { "length_beats", 1 } },
        { { "pitch", 64 }, { "start_beat", 2 }, { "length_beats", 1 } } } } });
    REQUIRE (rig.session.undoLabel() == "AI: add midi notes");
    rig.ok ("undo");
    REQUIRE (rig.session.findClip (clip.get<project::ClipId>())->notes.empty());   // all three at once
    rig.ok ("redo");
    REQUIRE (rig.session.findClip (clip.get<project::ClipId>())->notes.size() == 3);

    REQUIRE (rig.error ("save_project") == "file_required");
    const auto saved = rig.ok ("save_project", { { "file", "My Song" } });
    REQUIRE (saved["name"] == "My Song");
    REQUIRE_FALSE (rig.ok ("get_project_state")["unsaved_changes"].get<bool>());

    rig.ok ("set_tempo", { { "bpm", 90 } });
    REQUIRE (rig.error ("open_project", { { "file", "My Song" } }) == "confirmation_required");
    const auto opened = rig.ok ("open_project", { { "file", "My Song" }, { "confirm", true } });
    REQUIRE (opened["tempo_bpm"] == 120.0);
    REQUIRE (opened["tracks"][1]["clips"][0]["note_count"] == 3);
    REQUIRE (rig.error ("open_project", { { "file", "Missing" } }) == "not_found");
}

TEST_CASE ("Export jobs report progress, can be cancelled, and unknown jobs are errors", "[api][export]")
{
    Rig rig;
    auto& drums = rig.session.getProject().tracks[0];
    rig.session.resizeClip (drums.clips[0].id, 64 * 3840);   // a long song
    rig.ok ("set_drum_pattern", { { "clip", drums.clips[0].id }, { "pattern", { { "kick", "x...x...x...x..." } } } });

    const auto job = rig.ok ("export_wav", { { "file", "long" }, { "sample_rate", 96000 }, { "bit_depth", 32 } })["job"];
    REQUIRE (rig.error ("export_wav", { { "file", "long" }, { "overwrite", true } }) == "busy");
    rig.ok ("cancel_export", { { "job", job } });
    const auto s = rig.waitForExport (job);
    REQUIRE (s["state"] == "cancelled");
    REQUIRE_FALSE (fs::exists (rig.root / "Music" / "long.wav"));

    REQUIRE (rig.error ("get_export_status", { { "job", "export-99" } }) == "not_found");
}

TEST_CASE ("Note names", "[api]")
{
    REQUIRE (api::parseNoteName ("C4") == 60);
    REQUIRE (api::parseNoteName ("c4") == 60);
    REQUIRE (api::parseNoteName ("A4") == 69);
    REQUIRE (api::parseNoteName ("F#3") == 54);
    REQUIRE (api::parseNoteName ("Bb2") == 46);
    REQUIRE (api::parseNoteName ("C-1") == 0);
    REQUIRE (api::parseNoteName ("G9") == 127);
    REQUIRE (api::parseNoteName ("H4") == -1);
    REQUIRE (api::parseNoteName ("C") == -1);
    REQUIRE (api::parseNoteName ("C4x") == -1);
    REQUIRE (api::noteName (61) == "C#4");
}

TEST_CASE ("Presets and master volume through the API", "[api]")
{
    Rig rig;
    const auto instruments = rig.ok ("list_instruments");
    REQUIRE (instruments[0]["presets"].size() >= 5);
    const auto t = rig.ok ("set_instrument_preset", { { "track", 2 }, { "preset", "acid" }, { "instrument", "nebula_bass" } });
    REQUIRE (t["instrument"] == "nebula_bass");
    REQUIRE (t["preset"] == "Acid");
    REQUIRE (rig.error ("set_instrument_preset", { { "track", 2 }, { "preset", "dubstep" } }) == "invalid_argument");
    REQUIRE (rig.ok ("set_master_volume", { { "db", -3 } })["master_volume_db"] == -3.0);
    REQUIRE (rig.ok ("get_project_state")["master_volume_db"] == -3.0);
    REQUIRE (rig.error ("set_master_volume", { { "db", 20 } }) == "invalid_argument");
}
