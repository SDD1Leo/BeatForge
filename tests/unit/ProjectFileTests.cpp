#include <beatforge/project/ProjectFile.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>

using namespace beatforge::project;
using Catch::Matchers::ContainsSubstring;
namespace fs = std::filesystem;

namespace
{
    Project sampleProject()
    {
        Project p;
        p.name = "Neon Horizon";
        p.tempo = 97.5;
        p.timeSignature = { 7, 8 };
        p.metronomeEnabled = true;
        p.loopEnabled = true;
        p.loopBars = 2;
        p.lanes[0].name = "808 Kick";
        p.lanes[0].gainDb = -3.5f;
        p.lanes[1].muted = true;
        p.lanes[2].soloed = true;
        for (int s : { 0, 4, 8, 12 }) p.lanes[0].steps[(size_t) s] = true;
        for (int s = 0; s < 16; s += 2) p.lanes[2].steps[(size_t) s] = true;
        p.lanes[4].steps[15] = true;
        return p;
    }

    struct TempDir
    {
        TempDir() : path (fs::temp_directory_path() / ("beatforge-test-" + std::to_string (::getpid()) + "-" + std::to_string (counter++)))
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

    std::string minimal (const std::string& extraFields = "")
    {
        return R"({"format":"beatforge-project","version":1,"name":"T","tempo":120,"timeSignature":{"numerator":4,"denominator":4})"
               + extraFields + "}";
    }
}

TEST_CASE ("Project round-trips through JSON unchanged", "[project][file]")
{
    const auto original = sampleProject();
    const auto loaded = fromJson (toJson (original, "0.4.0"));

    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.empty());
    REQUIRE (*loaded.project == original);
}

TEST_CASE ("The file is readable JSON with a version and step strings", "[project][file]")
{
    const auto j = nlohmann::json::parse (toJson (sampleProject(), "0.4.0"));
    REQUIRE (j["format"] == "beatforge-project");
    REQUIRE (j["version"] == 1);
    REQUIRE (j["savedBy"] == "BeatForge 0.4.0");
    REQUIRE (j["tempo"] == 97.5);
    REQUIRE (j["tracks"][0]["type"] == "drumMachine");
    REQUIRE (j["tracks"][0]["lanes"][0]["sound"] == "kick");
    REQUIRE (j["tracks"][0]["lanes"][0]["name"] == "808 Kick");
    REQUIRE (j["tracks"][0]["lanes"][0]["steps"] == "x...x...x...x...");
}

TEST_CASE ("Save and load a file", "[project][file]")
{
    TempDir dir;
    const auto file = dir.path / "beat.beatforge";

    REQUIRE (saveFile (sampleProject(), file, "0.4.0").ok());
    REQUIRE (fs::exists (file));
    REQUIRE_FALSE (fs::exists (dir.path / "beat.beatforge.tmp"));

    const auto loaded = loadFile (file);
    REQUIRE (loaded.ok());
    REQUIRE (*loaded.project == sampleProject());

    // Saving again replaces the file.
    auto changed = sampleProject();
    changed.tempo = 140.0;
    REQUIRE (saveFile (changed, file, "0.4.0").ok());
    REQUIRE (loadFile (file).project->tempo == 140.0);
}

TEST_CASE ("A failed save leaves the existing file untouched", "[project][file]")
{
    TempDir dir;
    const auto file = dir.path / "beat.beatforge";
    REQUIRE (saveFile (sampleProject(), file, "0.4.0").ok());
    const auto before = read (file);

    fs::permissions (dir.path, fs::perms::owner_read | fs::perms::owner_exec);   // no new files allowed
    auto changed = sampleProject();
    changed.name = "Should not be written";
    const auto result = saveFile (changed, file, "0.4.0");
    fs::permissions (dir.path, fs::perms::owner_all);

    if (::geteuid() != 0)   // root ignores permissions
    {
        REQUIRE_FALSE (result.ok());
        REQUIRE (read (file) == before);
    }
}

TEST_CASE ("Saving into a missing folder reports an error", "[project][file]")
{
    TempDir dir;
    const auto result = saveFile (sampleProject(), dir.path / "nope" / "beat.beatforge", "0.4.0");
    REQUIRE_FALSE (result.ok());
    REQUIRE_THAT (result.error, ContainsSubstring ("Could not write"));
}

TEST_CASE ("Files that are not projects are refused with a reason", "[project][file]")
{
    SECTION ("not JSON")       { REQUIRE_THAT (fromJson ("hello {").error, ContainsSubstring ("not valid JSON")); }
    SECTION ("empty")          { REQUIRE_FALSE (fromJson ("").ok()); }
    SECTION ("JSON array")     { REQUIRE_THAT (fromJson ("[1,2]").error, ContainsSubstring ("not a BeatForge project")); }
    SECTION ("other format")   { REQUIRE_THAT (fromJson (R"({"format":"something-else","version":1})").error, ContainsSubstring ("not a BeatForge project")); }
    SECTION ("no version")     { REQUIRE_THAT (fromJson (R"({"format":"beatforge-project"})").error, ContainsSubstring ("no valid format version")); }
    SECTION ("missing file")   { REQUIRE_THAT (loadFile ("/nonexistent/beat.beatforge").error, ContainsSubstring ("does not exist")); }
}

TEST_CASE ("A project from a newer BeatForge is refused, not half-read", "[project][file][version]")
{
    const auto result = fromJson (R"({"format":"beatforge-project","version":2,"name":"Future"})");
    REQUIRE_FALSE (result.ok());
    REQUIRE_THAT (result.error, ContainsSubstring ("newer BeatForge"));
    REQUIRE_THAT (result.error, ContainsSubstring ("version 2"));
}

TEST_CASE ("Minimal version-1 file loads with defaults and says what was missing", "[project][file][version]")
{
    const auto result = fromJson (minimal());
    REQUIRE (result.ok());
    REQUIRE (result.project->name == "T");
    REQUIRE (result.project->lanes == Project::defaultLanes());
    REQUIRE (result.warnings.size() == 1);
    REQUIRE_THAT (result.warnings[0], ContainsSubstring ("no drum machine track"));
}

TEST_CASE ("Out-of-range values are corrected with warnings", "[project][file]")
{
    const auto result = fromJson (R"({"format":"beatforge-project","version":1,"name":"T","tempo":500,
        "timeSignature":{"numerator":4,"denominator":3},"loop":{"enabled":true,"bars":3},
        "tracks":[{"type":"drumMachine","name":"Drums","lanes":[
            {"sound":"kick","name":"K","gainDb":20,"muted":"yes","steps":"x.x"}]}]})");

    REQUIRE (result.ok());
    const auto& p = *result.project;
    REQUIRE (p.tempo == 240.0);
    REQUIRE (p.timeSignature == beatforge::time::TimeSignature { 4, 4 });
    REQUIRE (p.loopEnabled);
    REQUIRE (p.loopBars == 4);
    REQUIRE (p.lanes[0].gainDb == 6.0f);
    REQUIRE_FALSE (p.lanes[0].muted);
    REQUIRE (p.lanes[0].steps[0]);
    REQUIRE_FALSE (p.lanes[0].steps[1]);
    REQUIRE (p.lanes[0].steps[2]);
    REQUIRE_FALSE (p.lanes[0].steps[3]);   // short string: remaining steps off

    REQUIRE (result.warnings.size() == 5);   // tempo, signature, loop bars, gain, mute
}

TEST_CASE ("Unknown data survives a load/save cycle", "[project][file][compat]")
{
    const auto text = R"({"format":"beatforge-project","version":1,"name":"T","tempo":120,
        "timeSignature":{"numerator":4,"denominator":4},
        "futureFeature":{"mood":"dark"},
        "tracks":[
          {"type":"drumMachine","name":"Drums","swing":15,"lanes":[
            {"sound":"kick","name":"Kick","steps":"x...x...x...x...x.x.x.x.x.x.x.x.","probability":[1,1]},
            {"sound":"cowbell","name":"Cowbell","steps":"x..............."}]},
          {"type":"audio","name":"Vocals","clips":[1,2,3]}]})";

    const auto first = fromJson (text);
    REQUIRE (first.ok());
    REQUIRE (first.warnings.size() == 3);   // 32 steps, unknown lane, unknown track
    REQUIRE (first.project->lanes[0].steps[0]);

    // ordered_json keeps the file's key order, as the app does.
    const auto resaved = nlohmann::ordered_json::parse (toJson (*first.project, "0.4.0"));
    REQUIRE (resaved["futureFeature"]["mood"] == "dark");
    REQUIRE (resaved["tracks"][0]["swing"] == 15);
    REQUIRE (resaved["tracks"][0]["lanes"][0]["probability"] == nlohmann::ordered_json::array ({ 1, 1 }));
    REQUIRE (resaved["tracks"][0]["lanes"][0]["stepsAfter16"] == "x.x.x.x.x.x.x.x.");
    REQUIRE (resaved["tracks"][0]["lanes"][5]["sound"] == "cowbell");
    REQUIRE (resaved["tracks"][1]["type"] == "instrument");   // the synth track is always written
    REQUIRE (resaved["tracks"][2]["type"] == "audio");
    REQUIRE (resaved["tracks"][2]["clips"] == nlohmann::ordered_json::array ({ 1, 2, 3 }));

    // And it stays stable on the next cycle.
    const auto second = fromJson (resaved.dump());
    REQUIRE (second.ok());
    REQUIRE (*second.project == *first.project);
}

TEST_CASE ("Missing optional settings load silently", "[project][file]")
{
    const auto result = fromJson (minimal (R"(,"tracks":[{"type":"drumMachine","name":"Drums","lanes":[]}])"));
    REQUIRE (result.ok());
    REQUIRE (result.warnings.empty());
    REQUIRE_FALSE (result.project->metronomeEnabled);
    REQUIRE_FALSE (result.project->loopEnabled);
}
