#include <beatforge/project/ProjectFile.h>
#include <beatforge/project/Session.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace beatforge;
using project::Session;
using Catch::Matchers::ContainsSubstring;

TEST_CASE ("Creating a clip and editing notes through the session", "[session][midi]")
{
    audio::AudioEngine engine;
    Session session (engine);
    std::vector<Session::Change> changes;
    session.addListener ([&] (Session::Change c) { changes.push_back (c); });

    REQUIRE (session.addNote ({ 0, 60, 100, 0, 240 }) == 0);   // no clip yet
    session.createMidiClip (4);
    REQUIRE (session.getProject().synthClip->length == 4 * 3840);
    REQUIRE (session.getMidiClipBars() == 4);

    const auto a = session.addNote ({ 0, 60, 100, 0, 240 });
    const auto b = session.addNote ({ 0, 64, 90, 960, 480 });
    REQUIRE ((a != 0 && b != 0 && a != b));
    REQUIRE (session.isDirty());
    REQUIRE (std::count (changes.begin(), changes.end(), Session::Change::notes) == 3);

    auto moved = *session.findNote (b);
    moved.start = 1000;
    moved.pitch = 65;
    moved.velocity = 127;
    session.updateNotes ({ moved });
    REQUIRE (*session.findNote (b) == moved);

    session.quantizeNotes ({ b }, midi::quarter);
    REQUIRE (session.findNote (b)->start == 960);

    session.removeNotes ({ a });
    REQUIRE (session.findNote (a) == nullptr);
    REQUIRE (session.getProject().synthClip->notes.size() == 1);
}

TEST_CASE ("Notes outside a shortened clip are kept", "[session][midi]")
{
    audio::AudioEngine engine;
    Session session (engine);
    session.createMidiClip (4);
    const auto late = session.addNote ({ 0, 60, 100, 3 * 3840, 240 });   // bar 4

    session.setMidiClipBars (1);
    REQUIRE (session.findNote (late) != nullptr);
    session.setMidiClipBars (4);
    REQUIRE (session.findNote (late)->start == 3 * 3840);
}

TEST_CASE ("Clip and notes survive save and load", "[session][midi][file]")
{
    audio::AudioEngine engine;
    Session session (engine);
    session.createMidiClip (2);
    session.addNote ({ 0, 60, 100, 0, 240 });
    session.addNote ({ 0, 67, 45, 1440, 1200 });

    const auto json = project::toJson (session.getProject(), "test");
    const auto j = nlohmann::json::parse (json);
    REQUIRE (j["tracks"][1]["type"] == "instrument");
    REQUIRE (j["tracks"][1]["clip"]["lengthTicks"] == 7680);
    REQUIRE (j["tracks"][1]["clip"]["notes"][1] == nlohmann::json ({ { "pitch", 67 }, { "velocity", 45 }, { "start", 1440 }, { "length", 1200 } }));

    const auto loaded = project::fromJson (json);
    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.empty());

    audio::AudioEngine engine2;
    Session reopened (engine2);
    reopened.openProject (*loaded.project, std::nullopt);
    REQUIRE (reopened.getProject().synthClip->notes.size() == 2);
    REQUIRE (reopened.getProject().synthClip->notes[1].pitch == 67);
    REQUIRE (reopened.getProject().synthClip->notes[1].id != 0);
    REQUIRE (reopened.getMidiClipBars() == 2);
}

TEST_CASE ("Bad notes are corrected or kept verbatim, never dropped", "[midi][file]")
{
    const auto result = project::fromJson (R"({"format":"beatforge-project","version":1,"name":"T","tempo":120,
        "timeSignature":{"numerator":4,"denominator":4},
        "tracks":[{"type":"drumMachine","name":"Drums","lanes":[]},
          {"type":"instrument","instrument":"basicSynth","name":"Lead","clip":{"lengthTicks":3840,"color":"cyan","notes":[
            {"pitch":60,"velocity":100,"start":0,"length":240,"probability":0.5},
            {"pitch":300,"velocity":0,"start":240,"length":240},
            {"pitch":"C4","start":480,"length":240},
            {"pitch":62,"start":-5,"length":240}]}}]})");

    REQUIRE (result.ok());
    const auto& p = *result.project;
    REQUIRE (p.synthTrackName == "Lead");
    REQUIRE (p.synthClip->notes.size() == 2);
    REQUIRE (p.synthClip->notes[1].pitch == 127);
    REQUIRE (p.synthClip->notes[1].velocity == 1);
    REQUIRE (result.warnings.size() == 2);
    REQUIRE_THAT (result.warnings[0], ContainsSubstring ("out of range"));
    REQUIRE_THAT (result.warnings[1], ContainsSubstring ("2 synth note(s) were unreadable"));

    const auto resaved = nlohmann::ordered_json::parse (project::toJson (p, "test"));
    const auto& notes = resaved["tracks"][1]["clip"]["notes"];
    REQUIRE (notes.size() == 4);                          // 2 playable + 2 kept verbatim
    REQUIRE (notes[0]["probability"] == 0.5);             // unknown note key kept
    REQUIRE (notes[2]["pitch"] == "C4");
    REQUIRE (resaved["tracks"][1]["clip"]["color"] == "cyan");
}
