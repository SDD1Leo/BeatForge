#include <beatforge/project/ProjectFile.h>
#include <beatforge/project/Session.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace beatforge;
using project::Session;
using Change = Session::Change;

namespace
{
    std::vector<float> render (audio::AudioEngine& engine, int numSamples)
    {
        std::vector<float> l (512), r (512), out;
        float* chans[] { l.data(), r.data() };
        for (int done = 0; done < numSamples; done += 512)
        {
            engine.process (chans, 2, 512);
            out.insert (out.end(), l.begin(), l.end());
        }
        return out;
    }
}

TEST_CASE ("Edits update the project, the engine and the dirty flag", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);
    std::vector<Change> changes;
    session.addListener ([&] (Change c) { changes.push_back (c); });

    REQUIRE_FALSE (session.isDirty());

    session.setStep (0, 4, true);
    REQUIRE (session.getProject().lanes[0].steps[4]);
    REQUIRE (engine.getDrumMachine().getStep (0, 4));
    REQUIRE (session.isDirty());
    REQUIRE (changes == std::vector<Change> { Change::pattern, Change::metadata });

    session.setTempo (133.0);
    REQUIRE (engine.getTempo() == 133.0);
    session.setTempo (999.0);
    REQUIRE (session.getProject().tempo == 240.0);

    session.setLaneGainDb (1, -6.0f);
    session.setLaneMuted (1, true);
    session.setLaneSoloed (2, true);
    REQUIRE (engine.getDrumMachine().getLaneGainDb (1) == -6.0f);
    REQUIRE (engine.getDrumMachine().isLaneMuted (1));
    REQUIRE (engine.getDrumMachine().isLaneSoloed (2));

    session.setMetronomeEnabled (true);
    REQUIRE (engine.isMetronomeEnabled());
}

TEST_CASE ("No-op edits do not mark the project dirty", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);
    int notifications = 0;
    session.addListener ([&] (Change) { ++notifications; });

    session.setTempo (120.0);
    session.setStep (0, 0, false);
    session.setTimeSignature ({ 4, 4 });
    session.clearPattern();
    session.setStep (9, 0, true);        // invalid lane
    session.setStep (0, 16, true);       // invalid step
    session.setTimeSignature ({ 4, 3 }); // invalid signature

    REQUIRE_FALSE (session.isDirty());
    REQUIRE (notifications == 0);
}

TEST_CASE ("Loop length follows the time signature", "[session][loop]")
{
    audio::AudioEngine engine;
    Session session (engine);

    session.setLoop (true, 2);
    REQUIRE (engine.isLoopEnabled());
    REQUIRE (engine.getLoopEnd() == 3840 * 2);

    session.setTimeSignature ({ 3, 4 });
    REQUIRE (engine.getLoopEnd() == 2880 * 2);

    session.setLoop (true, 3);           // invalid length keeps the previous one
    REQUIRE (session.getProject().loopBars == 2);
}

TEST_CASE ("Renaming validates names", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);

    REQUIRE (session.renameLane (0, "808 Kick"));
    REQUIRE (session.getProject().lanes[0].name == "808 Kick");
    REQUIRE_FALSE (session.renameLane (0, ""));
    REQUIRE_FALSE (session.renameLane (0, std::string (33, 'x')));
    REQUIRE_FALSE (session.renameLane (7, "Nope"));
    REQUIRE (session.getProject().lanes[0].name == "808 Kick");

    REQUIRE (session.renameProject ("Neon Horizon"));
    REQUIRE_FALSE (session.renameProject (""));
    REQUIRE (session.getProject().name == "Neon Horizon");
}

TEST_CASE ("Opening a project replaces engine state and clears dirty", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);
    session.setStep (3, 3, true);
    session.setLaneMuted (4, true);

    project::Project p;
    p.tempo = 90.0;
    p.timeSignature = { 6, 8 };
    p.lanes[0].steps[0] = true;
    p.lanes[1].gainDb = -12.0f;

    std::vector<Change> changes;
    session.addListener ([&] (Change c) { changes.push_back (c); });
    session.openProject (p, std::filesystem::path ("/tmp/x.beatforge"));

    REQUIRE_FALSE (session.isDirty());
    REQUIRE (changes == std::vector<Change> { Change::replaced });
    REQUIRE (session.getFile() == std::filesystem::path ("/tmp/x.beatforge"));
    REQUIRE (engine.getTempo() == 90.0);
    REQUIRE (engine.getTimeSignature() == time::TimeSignature { 6, 8 });
    REQUIRE (engine.getDrumMachine().getStep (0, 0));
    REQUIRE_FALSE (engine.getDrumMachine().getStep (3, 3));   // old pattern gone
    REQUIRE_FALSE (engine.getDrumMachine().isLaneMuted (4));
    REQUIRE (engine.getDrumMachine().getLaneGainDb (1) == -12.0f);

    session.newProject();
    REQUIRE (session.getProject() == project::Project {});
    REQUIRE_FALSE (session.getFile().has_value());
}

TEST_CASE ("A saved and reopened beat sounds bit-identical", "[session][file]")
{
    const auto file = std::filesystem::temp_directory_path() / "beatforge-session-roundtrip.beatforge";

    std::vector<float> original;
    {
        audio::AudioEngine engine;
        engine.prepare (48000.0);
        Session session (engine);
        session.setTempo (126.0);
        for (int s : { 0, 4, 8, 12 }) session.setStep (0, s, true);
        for (int s : { 4, 12 })       session.setStep (4, s, true);
        for (int s = 0; s < 16; ++s)  session.setStep (2, s, true);
        session.setStep (3, 14, true);
        session.setLaneGainDb (2, -4.0f);
        session.setLoop (true, 1);

        REQUIRE (project::saveFile (session.getProject(), file, "test").ok());
        session.markSaved (file);
        REQUIRE_FALSE (session.isDirty());

        engine.play();
        original = render (engine, 48000 * 5);
    }

    audio::AudioEngine engine;
    engine.prepare (48000.0);
    Session session (engine);
    auto loaded = project::loadFile (file);
    REQUIRE (loaded.ok());
    session.openProject (*loaded.project, file);
    engine.play();

    REQUIRE (render (engine, 48000 * 5) == original);
    std::filesystem::remove (file);
}
