#include <beatforge/project/ProjectFile.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include "TestSupport.h"

using namespace beatforge::project;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

namespace
{
    // Ids as the reader assigns them: tracks 1.., clips 1.. across the project, notes 1.. per clip.
    Project sampleProject()
    {
        Project p;
        p.name = "Neon Horizon";
        p.tempo = 97.5;
        p.timeSignature = { 7, 8 };
        p.metronomeEnabled = true;
        p.loopEnabled = true;
        p.loopStart = 3840;
        p.loopEnd = 3 * 3840;

        Track drums;
        drums.id = 1;
        drums.type = TrackType::drums;
        drums.name = "Beat";
        drums.volumeDb = -2.5f;
        drums.pan = -0.25f;
        drums.lanes[0].name = "808 Kick";
        drums.lanes[1].muted = true;
        Clip beat;
        beat.id = 1;
        beat.length = 2 * 3840;
        for (int s : { 0, 4, 8, 12 }) beat.pattern[0][(size_t) s] = true;
        beat.pattern[4][12] = true;
        Clip fill = beat;
        fill.id = 2;
        fill.start = 2 * 3840;
        fill.length = 3840;
        fill.pattern[1][15] = true;
        drums.clips = { beat, fill };

        Track bass;
        bass.id = 2;
        bass.type = TrackType::instrument;
        bass.name = "Bass";
        bass.soloed = true;
        bass.instrument = beatforge::audio::InstrumentType::monoBass;
        bass.instrumentParams[1][(size_t) beatforge::audio::Param::cutoff] = 650.0f;
        Clip line;
        line.id = 3;
        line.start = 3840;
        line.length = 3840;
        line.notes = { { 1, 36, 100, 0, 480 }, { 2, 43, 90, 960, 480 } };
        bass.clips = { line };

        p.tracks = { drums, bass };
        return p;
    }

    struct TempDir
    {
        TempDir() : path (fs::temp_directory_path() / ("beatforge-test-" + std::to_string (beatforge::test::processId()) + "-" + std::to_string (counter++)))
        {
            fs::create_directories (path);
        }
        ~TempDir() { std::error_code ec; fs::permissions (path, fs::perms::owner_all, ec); fs::remove_all (path, ec); }
        fs::path path;
        static inline int counter = 0;
    };

    std::string read (const fs::path& p)
    {
        std::ifstream in (p);
        return { std::istreambuf_iterator<char> (in), {} };
    }

    const char* v1House = R"({"format":"beatforge-project","version":1,"name":"House Demo","tempo":124,
        "timeSignature":{"numerator":4,"denominator":4},"metronome":false,"loop":{"enabled":false,"bars":4},
        "tracks":[{"type":"drumMachine","name":"Drums","lanes":[
            {"sound":"kick","name":"808","gainDb":-1.5,"muted":false,"soloed":false,"steps":"x...x...x...x..."},
            {"sound":"clap","name":"Clap","gainDb":0,"muted":true,"soloed":false,"steps":"....x.......x..."}]},
          {"type":"instrument","instrument":"monoBass","name":"Bass","params":{"monoBass":{"cutoff":500}},
           "clip":{"lengthTicks":7680,"notes":[{"pitch":36,"velocity":110,"start":480,"length":480}]}}]})";
}

TEST_CASE ("Project round-trips through JSON unchanged", "[project][file]")
{
    const auto original = sampleProject();
    const auto loaded = fromJson (toJson (original, "0.7.0"));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.fileVersion == 2);
    REQUIRE (loaded.warnings.empty());
    REQUIRE (*loaded.project == original);
}

TEST_CASE ("The file is readable JSON with tracks, clips and step strings", "[project][file]")
{
    const auto j = nlohmann::json::parse (toJson (sampleProject(), "0.7.0"));
    REQUIRE (j["format"] == "beatforge-project");
    REQUIRE (j["version"] == 2);
    REQUIRE (j["loop"] == nlohmann::json ({ { "enabled", true }, { "start", 3840 }, { "end", 11520 } }));
    REQUIRE (j["tracks"][0]["type"] == "drumMachine");
    REQUIRE (j["tracks"][0]["lanes"][0]["name"] == "808 Kick");
    REQUIRE (j["tracks"][0]["clips"][0]["pattern"]["kick"] == "x...x...x...x...");
    REQUIRE (j["tracks"][0]["clips"][1]["start"] == 7680);
    REQUIRE (j["tracks"][1]["instrument"] == "monoBass");
    REQUIRE (j["tracks"][1]["clips"][0]["notes"][1]["pitch"] == 43);
}

TEST_CASE ("Save and load a file", "[project][file]")
{
    TempDir dir;
    const auto file = dir.path / "beat.beatforge";

    REQUIRE (saveFile (sampleProject(), file, "0.7.0").ok());
    REQUIRE_FALSE (fs::exists (dir.path / "beat.beatforge.tmp"));
    REQUIRE (*loadFile (file).project == sampleProject());

    auto changed = sampleProject();
    changed.tempo = 140.0;
    REQUIRE (saveFile (changed, file, "0.7.0").ok());
    REQUIRE (loadFile (file).project->tempo == 140.0);
}

TEST_CASE ("A failed save leaves the existing file untouched", "[project][file]")
{
    TempDir dir;
    const auto file = dir.path / "beat.beatforge";
    REQUIRE (saveFile (sampleProject(), file, "0.7.0").ok());
    const auto before = read (file);

    fs::permissions (dir.path, fs::perms::owner_read | fs::perms::owner_exec);
    auto changed = sampleProject();
    changed.name = "Should not be written";
    const auto result = saveFile (changed, file, "0.7.0");
    fs::permissions (dir.path, fs::perms::owner_all);

    if (::geteuid() != 0)
    {
        REQUIRE_FALSE (result.ok());
        REQUIRE_THAT (result.error, ContainsSubstring ("Permission denied"));
        REQUIRE (read (file) == before);
    }
}

TEST_CASE ("Files that are not projects are refused with a reason", "[project][file]")
{
    SECTION ("not JSON")      { REQUIRE_THAT (fromJson ("hello {").error, ContainsSubstring ("not valid JSON")); }
    SECTION ("JSON array")    { REQUIRE_THAT (fromJson ("[1,2]").error, ContainsSubstring ("not a BeatForge project")); }
    SECTION ("no version")    { REQUIRE_THAT (fromJson (R"({"format":"beatforge-project"})").error, ContainsSubstring ("no valid format version")); }
    SECTION ("missing file")  { REQUIRE_THAT (loadFile ("/nonexistent/beat.beatforge").error, ContainsSubstring ("does not exist")); }
    SECTION ("missing folder")
    {
        TempDir dir;
        REQUIRE_THAT (saveFile (sampleProject(), dir.path / "nope" / "x.beatforge", "t").error, ContainsSubstring ("Could not write"));
    }
}

TEST_CASE ("A project from a newer BeatForge is refused, not half-read", "[project][file][version]")
{
    const auto result = fromJson (R"({"format":"beatforge-project","version":3,"name":"Future"})");
    REQUIRE_FALSE (result.ok());
    REQUIRE_THAT (result.error, ContainsSubstring ("newer BeatForge"));
    REQUIRE_THAT (result.error, ContainsSubstring ("version 3"));
}

TEST_CASE ("Version 1 projects are upgraded to the arrangement", "[project][file][version]")
{
    const auto r = fromJson (v1House);
    REQUIRE (r.ok());
    REQUIRE (r.fileVersion == 1);
    REQUIRE (r.warnings.empty());
    const auto& p = *r.project;

    REQUIRE (p.tracks.size() == 2);
    const auto& drums = p.tracks[0];
    REQUIRE (drums.type == TrackType::drums);
    REQUIRE (drums.lanes[0].name == "808");
    REQUIRE (drums.lanes[0].gainDb == -1.5f);
    REQUIRE (drums.lanes[4].muted);
    REQUIRE (drums.clips.size() == 1);
    REQUIRE (drums.clips[0].start == 0);
    REQUIRE (drums.clips[0].length == 7680);          // covers the 2-bar synth clip
    REQUIRE (drums.clips[0].pattern[0][4]);
    REQUIRE (drums.clips[0].pattern[4][12]);

    const auto& bass = p.tracks[1];
    REQUIRE (bass.instrument == beatforge::audio::InstrumentType::monoBass);
    REQUIRE (bass.instrumentParams[1][(size_t) beatforge::audio::Param::cutoff] == 500.0f);
    REQUIRE (bass.clips.size() == 1);
    REQUIRE (bass.clips[0].length == 7680);
    REQUIRE (bass.clips[0].notes.size() == 1);

    // Everything looped forever in 0.6: the loop now covers the song.
    REQUIRE (p.loopEnabled);
    REQUIRE (p.loopStart == 0);
    REQUIRE (p.loopEnd == 7680);

    // Saving writes version 2, which loads back the same.
    const auto again = fromJson (toJson (p, "0.7.0"));
    REQUIRE (again.fileVersion == 2);
    REQUIRE (*again.project == p);
}

TEST_CASE ("A version 1 loop shorter than the song is kept", "[project][file][version]")
{
    std::string text = v1House;
    text.replace (text.find (R"("enabled":false,"bars":4)"), 24, R"("enabled":true,"bars":1 )");
    const auto r = fromJson (text);
    REQUIRE (r.ok());
    REQUIRE (r.project->loopEnd == 3840);
}

TEST_CASE ("Out-of-range values are corrected with warnings", "[project][file]")
{
    const auto r = fromJson (R"({"format":"beatforge-project","version":2,"name":"T","tempo":500,
        "timeSignature":{"numerator":4,"denominator":3},"loop":{"enabled":true,"start":7680,"end":3840},
        "tracks":[{"type":"drumMachine","name":"Drums","volumeDb":40,"pan":-7,"lanes":[{"sound":"kick","gainDb":20}],
                   "clips":[{"start":0,"length":3840,"pattern":{"kick":"x.?x"}}]}]})");
    REQUIRE (r.ok());
    const auto& p = *r.project;
    REQUIRE (p.tempo == 240.0);
    REQUIRE (p.timeSignature == beatforge::time::TimeSignature { 4, 4 });
    REQUIRE (p.loopStart == 0);
    REQUIRE (p.loopEnd == 4 * 3840);
    REQUIRE (p.tracks[0].volumeDb == 6.0f);
    REQUIRE (p.tracks[0].pan == -1.0f);
    REQUIRE (p.tracks[0].lanes[0].gainDb == 6.0f);
    REQUIRE (p.tracks[0].clips[0].pattern[0][0]);
    REQUIRE_FALSE (p.tracks[0].clips[0].pattern[0][2]);
    REQUIRE (r.warnings.size() == 7);   // tempo, signature, loop, volume, pan, lane gain, odd step character
}

TEST_CASE ("Unknown data survives a load/save cycle", "[project][file][compat]")
{
    const auto text = R"({"format":"beatforge-project","version":2,"name":"T","tempo":120,
        "timeSignature":{"numerator":4,"denominator":4},"futureFeature":{"mood":"dark"},
        "tracks":[
          {"type":"drumMachine","name":"Drums","swing":15,"lanes":[{"sound":"kick","name":"Kick","accent":1}],
           "clips":[{"start":0,"length":3840,"color":"pink","pattern":{"kick":"x...x...x...x...x.x.","cowbell":"x..."}}]},
          {"type":"instrument","instrument":"polySynth","name":"Keys","params":{"polySynth":{"drive":0.5},"fmSynth":{"ratio":2}},
           "clips":[{"start":0,"length":3840,"notes":[{"pitch":60,"start":0,"length":240,"probability":0.5},{"pitch":"C4"}]},
                    {"start":-3,"length":10}]},
          {"type":"audio","name":"Vocals","clips":[1,2,3]}]})";

    const auto first = fromJson (text);
    REQUIRE (first.ok());
    REQUIRE (first.warnings.size() == 4);   // steps beyond 16, unreadable note, unreadable clip, unknown track

    const auto resaved = ojson::parse (toJson (*first.project, "0.7.0"));
    REQUIRE (resaved["futureFeature"]["mood"] == "dark");
    REQUIRE (resaved["tracks"][0]["swing"] == 15);
    REQUIRE (resaved["tracks"][0]["lanes"][0]["accent"] == 1);
    REQUIRE (resaved["tracks"][0]["clips"][0]["color"] == "pink");
    REQUIRE (resaved["tracks"][0]["clips"][0]["pattern"]["cowbell"] == "x...");
    REQUIRE (resaved["tracks"][0]["clips"][0]["kick_after16"] == "x.x.");
    REQUIRE (resaved["tracks"][1]["params"]["polySynth"]["drive"] == 0.5);
    REQUIRE (resaved["tracks"][1]["params"]["fmSynth"]["ratio"] == 2);
    REQUIRE (resaved["tracks"][1]["clips"][0]["notes"][0]["probability"] == 0.5);
    REQUIRE (resaved["tracks"][1]["clips"][0]["notes"][1]["pitch"] == "C4");
    REQUIRE (resaved["tracks"][1]["clips"][1]["start"] == -3);
    REQUIRE (resaved["tracks"][2]["type"] == "audio");

    const auto second = fromJson (resaved.dump());
    REQUIRE (second.ok());
    REQUIRE (*second.project == *first.project);
}
