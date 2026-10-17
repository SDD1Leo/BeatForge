#include <beatforge/time/MusicalTime.h>

#include <catch2/catch_test_macros.hpp>

using namespace beatforge::time;

TEST_CASE ("Beat and bar lengths follow the time signature", "[time]")
{
    REQUIRE (ppq == 960);
    REQUIRE (TimeSignature { 4, 4 }.ticksPerBeat() == 960);
    REQUIRE (TimeSignature { 4, 4 }.ticksPerBar() == 3840);
    REQUIRE (TimeSignature { 3, 4 }.ticksPerBar() == 2880);
    REQUIRE (TimeSignature { 6, 8 }.ticksPerBeat() == 480);
    REQUIRE (TimeSignature { 6, 8 }.ticksPerBar() == 2880);
    REQUIRE (TimeSignature { 7, 8 }.ticksPerBar() == 3360);
    REQUIRE (TimeSignature { 2, 2 }.ticksPerBeat() == 1920);
}

TEST_CASE ("Time signature validation", "[time]")
{
    REQUIRE (TimeSignature { 4, 4 }.isValid());
    REQUIRE (TimeSignature { 7, 8 }.isValid());
    REQUIRE_FALSE (TimeSignature { 0, 4 }.isValid());
    REQUIRE_FALSE (TimeSignature { 4, 3 }.isValid());
    REQUIRE_FALSE (TimeSignature { 4, 0 }.isValid());
    REQUIRE_FALSE (TimeSignature { 33, 4 }.isValid());
    REQUIRE_FALSE (TimeSignature { 4, 64 }.isValid());
}

TEST_CASE ("Ticks convert to bars, beats and ticks", "[time]")
{
    const TimeSignature fourFour { 4, 4 };
    REQUIRE (toBarBeatTick (0, fourFour) == BarBeatTick { 1, 1, 0 });
    REQUIRE (toBarBeatTick (959, fourFour) == BarBeatTick { 1, 1, 959 });
    REQUIRE (toBarBeatTick (960, fourFour) == BarBeatTick { 1, 2, 0 });
    REQUIRE (toBarBeatTick (3840, fourFour) == BarBeatTick { 2, 1, 0 });
    REQUIRE (toBarBeatTick (3840 * 3 + 960 * 2 + 480, fourFour) == BarBeatTick { 4, 3, 480 });

    const TimeSignature sixEight { 6, 8 };
    REQUIRE (toBarBeatTick (480 * 5, sixEight) == BarBeatTick { 1, 6, 0 });
    REQUIRE (toBarBeatTick (480 * 6, sixEight) == BarBeatTick { 2, 1, 0 });

    const TimeSignature threeFour { 3, 4 };
    REQUIRE (toBarBeatTick (2880 * 2 + 10, threeFour) == BarBeatTick { 3, 1, 10 });
}

TEST_CASE ("Negative positions count back into bar 0", "[time]")
{
    REQUIRE (toBarBeatTick (-1, { 4, 4 }) == BarBeatTick { 0, 4, 959 });
    REQUIRE (toBarBeatTick (-3840, { 4, 4 }) == BarBeatTick { 0, 1, 0 });
}

TEST_CASE ("Bar/beat/tick round-trips", "[time]")
{
    for (auto sig : { TimeSignature { 4, 4 }, TimeSignature { 3, 4 }, TimeSignature { 6, 8 }, TimeSignature { 7, 8 }, TimeSignature { 5, 4 } })
        for (Tick t = -5000; t < 50000; t += 37)
            REQUIRE (fromBarBeatTick (toBarBeatTick (t, sig), sig) == t);
}
