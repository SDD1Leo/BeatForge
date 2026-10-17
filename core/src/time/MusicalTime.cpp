#include "beatforge/time/MusicalTime.h"

namespace beatforge::time
{
    bool TimeSignature::isValid() const noexcept
    {
        const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
        return numerator >= 1 && numerator <= 32 && powerOfTwo && denominator <= 32;
    }

    BarBeatTick toBarBeatTick (Tick position, TimeSignature sig) noexcept
    {
        const auto perBar = sig.ticksPerBar();
        const auto perBeat = sig.ticksPerBeat();

        // Floor division so negative positions (pre-roll) stay consistent.
        auto bar = position / perBar;
        auto inBar = position % perBar;
        if (inBar < 0) { inBar += perBar; --bar; }

        return { bar + 1, (int) (inBar / perBeat) + 1, inBar % perBeat };
    }

    Tick fromBarBeatTick (BarBeatTick bbt, TimeSignature sig) noexcept
    {
        return (bbt.bar - 1) * sig.ticksPerBar() + (Tick) (bbt.beat - 1) * sig.ticksPerBeat() + bbt.tick;
    }
}
