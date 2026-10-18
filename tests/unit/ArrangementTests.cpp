#include "AllocationGuard.h"
#include "EngineHelpers.h"

#include <beatforge/audio/AudioEngine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beatforge;
using beatforge::test::peakIn;
using beatforge::test::render;
using Catch::Approx;

namespace
{
    // One kick on every beat of slot 0 and a held note on slot 1, for mixing tests.
    void kicksAndPad (audio::AudioEngine& e)
    {
        e.prepare (48000.0);
        test::playPattern (e, 0, test::pattern ({ { 0, { 0, 4, 8, 12 } } }), 16);
        test::playNotes (e, 1, { { 0, 57, 100, 0, 3840 * 16 } }, 3840 * 16, false);
    }

    std::vector<float> renderRight (audio::AudioEngine& e, int numSamples)
    {
        std::vector<float> l (512), r (512), out;
        float* ch[] { l.data(), r.data() };
        for (int done = 0; done < numSamples; done += 512)
        {
            e.process (ch, 2, 512);
            out.insert (out.end(), r.begin(), r.end());
        }
        return out;
    }
}

TEST_CASE ("Clips play where they sit on the timeline", "[arrangement]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);

    project::Track t;
    t.type = project::TrackType::drums;
    project::Clip c;
    c.id = 1;
    c.start = 2 * 3840;            // bar 3
    c.length = 3840;
    c.pattern[0][0] = true;
    t.clips = { c };
    engine.setSlotType (0, audio::SlotType::drums);
    engine.setSlotSchedule (0, project::buildSchedule (t));

    engine.play();
    const auto out = render (engine, 96000 * 4);
    REQUIRE (peakIn (out, 0, 192000) == 0.0f);         // bars 1-2: silence
    REQUIRE (peakIn (out, 192000, 192010) > 0.0f);      // the kick at bar 3
    REQUIRE (peakIn (out, 288000 - 1000, 288000) == 0.0f);
}

TEST_CASE ("Tracks are mixed with volume, pan, mute and solo", "[arrangement][mix]")
{
    audio::AudioEngine reference;
    kicksAndPad (reference);
    reference.play();
    const auto both = render (reference, 48000);

    // Once the mix fade has landed (~5500 samples to within 1e-5), a muted or
    // un-soloed track contributes exactly nothing.
    auto sameAfterFade = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        return std::equal (a.begin() + 9600, a.end(), b.begin() + 9600);
    };

    SECTION ("muting a track removes it")
    {
        audio::AudioEngine e, kicksOnly;
        kicksAndPad (e);
        e.setSlotMix (1, 0.0f, 0.0f, true, false);
        kicksOnly.prepare (48000.0);
        test::playPattern (kicksOnly, 0, test::pattern ({ { 0, { 0, 4, 8, 12 } } }), 16);
        e.play();
        kicksOnly.play();
        const auto out = render (e, 48000);
        REQUIRE (out != both);
        REQUIRE (sameAfterFade (out, render (kicksOnly, 48000)));
    }

    SECTION ("solo isolates a track")
    {
        audio::AudioEngine e, padOnly;
        kicksAndPad (e);
        e.setSlotMix (1, 0.0f, 0.0f, false, true);
        padOnly.prepare (48000.0);
        test::playNotes (padOnly, 1, { { 0, 57, 100, 0, 3840 * 16 } }, 3840 * 16, false);
        e.play();
        padOnly.play();
        REQUIRE (sameAfterFade (render (e, 48000), render (padOnly, 48000)));
    }

    SECTION ("-6 dB halves a track")
    {
        audio::AudioEngine e, full;
        e.prepare (48000.0);
        full.prepare (48000.0);
        test::playNotes (e, 0, { { 0, 57, 100, 0, 3840 * 16 } }, 3840 * 16, false);
        test::playNotes (full, 0, { { 0, 57, 100, 0, 3840 * 16 } }, 3840 * 16, false);
        e.setSlotMix (0, -6.0206f, 0.0f, false, false);
        e.play();
        full.play();
        const auto quiet = render (e, 48000), loud = render (full, 48000);
        REQUIRE (peakIn (quiet, 24000) == Approx (peakIn (loud, 24000) * 0.5f).epsilon (0.01));
    }

    SECTION ("panning left silences the right side")
    {
        audio::AudioEngine e;
        e.prepare (48000.0);
        test::playNotes (e, 0, { { 0, 57, 100, 0, 3840 * 16 } }, 3840 * 16, false);
        e.setSlotMix (0, 0.0f, -1.0f, false, false);
        e.play();
        const auto right = renderRight (e, 48000);
        REQUIRE (peakIn (right, 4800) < 1e-4f);
    }
}

TEST_CASE ("An empty slot type plays nothing", "[arrangement]")
{
    audio::AudioEngine e;
    e.prepare (48000.0);
    test::playPattern (e, 3, test::pattern ({ { 0, { 0 } } }), 4);
    e.setSlotType (3, audio::SlotType::none);
    e.play();
    REQUIRE (peakIn (render (e, 96000)) == 0.0f);
}

TEST_CASE ("Seek moves the playhead; Stop returns to it", "[arrangement][transport]")
{
    audio::AudioEngine e;
    e.prepare (48000.0);
    test::playPattern (e, 0, test::pattern ({ { 0, { 0 } } }), 8);

    e.seek (3840.0 * 2);   // bar 3
    render (e, 512);
    REQUIRE (e.getPositionTicks() == Approx (7680.0));

    e.play();
    const auto out = render (e, 48000);
    REQUIRE (peakIn (out, 0, 10) > 0.0f);          // bar 3's kick plays at once
    REQUIRE (e.getPositionTicks() > 7680.0);

    e.stop();
    render (e, 512);
    REQUIRE (e.getPositionTicks() == Approx (7680.0));   // back to where play started

    e.play();
    render (e, 24000);
    e.seek (0.0);                                  // seek while playing
    const auto after = render (e, 512);
    REQUIRE (peakIn (after, 0, 10) > 0.0f);         // bar 1's kick
}

TEST_CASE ("Seeking while playing releases notes that no longer sound", "[arrangement][stuck]")
{
    audio::AudioEngine e;
    e.prepare (48000.0);
    test::playNotes (e, 0, { { 0, 60, 100, 0, 3840 } }, 3840 * 4, false);
    e.play();
    render (e, 12000);
    REQUIRE (e.getActiveVoices (0) == 1);
    e.seek (3840.0 * 2);
    render (e, 24000);
    REQUIRE (e.getActiveVoices (0) == 0);
}

TEST_CASE ("Slot changes and schedules stay allocation-free on the audio thread", "[arrangement][realtime]")
{
    audio::AudioEngine e;
    kicksAndPad (e);
    std::vector<float> l (8192), r (8192);
    float* ch[] { l.data(), r.data() };
    e.play();

    for (int round = 0; round < 3; ++round)
    {
        test::playPattern (e, 2, test::pattern ({ { 1, { 4, 12 } } }), 4);   // message thread: allocates by design
        e.collectGarbage();

        test::ScopedAllocationCounter allocations;
        for (int i = 0; i < 20; ++i)
            e.process (ch, 2, i % 2 == 0 ? 256 : 8192);   // also exercises chunking of large blocks
        e.setSlotMix (2, -3.0f, 0.3f, round == 1, false);
        e.setSlotType (2, round == 2 ? audio::SlotType::none : audio::SlotType::drums);
        REQUIRE (allocations.count() == 0);
    }
}

TEST_CASE ("Track meters show each track after its fader; muted tracks read silence", "[arrangement][mix]")
{
    audio::AudioEngine e;
    kicksAndPad (e);
    e.play();
    render (e, 48000);
    REQUIRE (e.getSlotMeter (0).takePeak (0) > 0.05f);
    REQUIRE (e.getSlotMeter (1).takePeak (0) > 0.01f);
    REQUIRE (e.getSlotMeter (5).takePeak (0) == 0.0f);   // unused slot

    e.setSlotMix (0, 0.0f, 0.0f, true, false);
    render (e, 9600);                                     // ramp
    e.getSlotMeter (0).takePeak (0);
    render (e, 48000);
    REQUIRE (e.getSlotMeter (0).takePeak (0) == 0.0f);
    REQUIRE (e.getSlotMeter (1).takePeak (0) > 0.01f);
}

TEST_CASE ("Master volume scales the tracks, not the metronome", "[arrangement][mix]")
{
    auto renderWith = [] (float masterDb, bool tracks, bool click)
    {
        audio::AudioEngine e;
        e.prepare (48000.0);
        if (tracks)
            test::playNotes (e, 0, { { 0, 57, 100, 0, 3840 * 4 } }, 3840 * 4, false);
        e.setMetronomeEnabled (click);
        e.setMasterVolume (masterDb);
        e.play();
        render (e, 9600);   // past the smoothing
        return peakIn (render (e, 24000));
    };

    const auto full = renderWith (0.0f, true, false), half = renderWith (-6.0206f, true, false);
    REQUIRE (half == Approx (full * 0.5f).epsilon (0.01));
    REQUIRE (renderWith (-60.0f, true, false) == 0.0f);
    REQUIRE (renderWith (-60.0f, false, true) == Approx (renderWith (0.0f, false, true)));   // the click stays
}
