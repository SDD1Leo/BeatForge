#include <beatforge/time/TempoMap.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace beatforge::time;
using Catch::Approx;

TEST_CASE ("Default tempo is 120 BPM and clamps to 40-240", "[time][tempo]")
{
    REQUIRE (TempoMap().tempoAt (0) == 120.0);
    REQUIRE (clampTempo (10.0) == 40.0);
    REQUIRE (clampTempo (500.0) == 240.0);
    REQUIRE (clampTempo (97.5) == 97.5);
}

TEST_CASE ("Constant-tempo conversions at several tempos and sample rates", "[time][tempo]")
{
    const double bpm = GENERATE (40.0, 100.0, 120.0, 133.0, 174.0, 240.0);
    const double rate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (bpm, rate);

    TempoMap map (bpm);
    const double secondsPerBeat = 60.0 / bpm;

    REQUIRE (map.ticksToSeconds (960.0) == Approx (secondsPerBeat));
    REQUIRE (map.ticksToSeconds (3840.0 * 4) == Approx (16 * secondsPerBeat));
    REQUIRE (map.ticksToSamples (960.0, rate) == Approx (secondsPerBeat * rate));
    REQUIRE (map.samplesToTicks (secondsPerBeat * rate, rate) == Approx (960.0));
    REQUIRE (map.secondsToTicks (map.ticksToSeconds (12345.0)) == Approx (12345.0));
}

TEST_CASE ("Known values: 120 BPM at 48 kHz is 25 samples per tick", "[time][tempo]")
{
    TempoMap map (120.0);
    REQUIRE (map.ticksToSamples (1.0, 48000.0) == Approx (25.0));
    REQUIRE (map.ticksToSamples (960.0, 48000.0) == Approx (24000.0));
    REQUIRE (TempoMap (100.0).ticksToSamples (960.0, 44100.0) == Approx (26460.0));
}

TEST_CASE ("Tempo changes are piecewise", "[time][tempo]")
{
    TempoMap map (120.0);
    REQUIRE_FALSE (map.addChange (3840, 60.0).has_value());     // bar 2 at 60 BPM

    REQUIRE (map.tempoAt (3839) == 120.0);
    REQUIRE (map.tempoAt (3840) == 60.0);

    // Bar 1: 4 beats * 0.5 s = 2 s. Bar 2: 4 beats * 1 s.
    REQUIRE (map.ticksToSeconds (3840.0) == Approx (2.0));
    REQUIRE (map.ticksToSeconds (3840.0 + 960.0) == Approx (3.0));
    REQUIRE (map.ticksToSeconds (7680.0) == Approx (6.0));

    REQUIRE (map.secondsToTicks (1.0) == Approx (1920.0));
    REQUIRE (map.secondsToTicks (3.0) == Approx (4800.0));

    for (double t = 0.0; t < 20000.0; t += 123.0)
        REQUIRE (map.secondsToTicks (map.ticksToSeconds (t)) == Approx (t));
}

TEST_CASE ("Tempo change validation and replacement", "[time][tempo]")
{
    TempoMap map (120.0);
    REQUIRE (map.addChange (-1, 100.0).has_value());
    REQUIRE (map.addChange (960, 39.9).has_value());
    REQUIRE (map.addChange (960, 240.1).has_value());
    REQUIRE (map.getChanges().size() == 1);

    REQUIRE_FALSE (map.addChange (1920, 90.0).has_value());
    REQUIRE_FALSE (map.addChange (960, 100.0).has_value());     // inserted before 1920
    REQUIRE_FALSE (map.addChange (960, 110.0).has_value());     // replaces
    REQUIRE (map.getChanges().size() == 3);
    REQUIRE (map.getChanges()[1].tick == 960);
    REQUIRE (map.getChanges()[1].bpm == 110.0);
    REQUIRE_FALSE (map.addChange (0, 80.0).has_value());       // replaces the initial tempo
    REQUIRE (map.tempoAt (0) == 80.0);
}
