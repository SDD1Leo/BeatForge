#pragma once

#include <cstdint>

namespace beatforge::time
{
    using Tick = std::int64_t;

    // Internal musical resolution: ticks per quarter note.
    constexpr Tick ppq = 960;

    struct TimeSignature
    {
        int numerator = 4;     // beats per bar, 1..32
        int denominator = 4;   // beat unit: 1, 2, 4, 8, 16 or 32

        bool isValid() const noexcept;

        // Length of one beat (one denominator note) and one bar, in ticks.
        Tick ticksPerBeat() const noexcept { return ppq * 4 / denominator; }
        Tick ticksPerBar() const noexcept  { return ticksPerBeat() * numerator; }

        bool operator== (const TimeSignature&) const = default;
    };

    // 1-based bar and beat, 0-based tick within the beat - as DAWs display it.
    struct BarBeatTick
    {
        std::int64_t bar = 1;
        int beat = 1;
        Tick tick = 0;

        bool operator== (const BarBeatTick&) const = default;
    };

    // Assumes one time signature from tick 0 (time-signature maps come later).
    BarBeatTick toBarBeatTick (Tick position, TimeSignature) noexcept;
    Tick fromBarBeatTick (BarBeatTick, TimeSignature) noexcept;
}
