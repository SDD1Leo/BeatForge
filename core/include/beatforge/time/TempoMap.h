#pragma once

#include "beatforge/time/MusicalTime.h"

#include <optional>
#include <string>
#include <vector>

namespace beatforge::time
{
    constexpr double minTempo = 40.0;
    constexpr double maxTempo = 240.0;
    constexpr double defaultTempo = 120.0;

    double clampTempo (double bpm) noexcept;

    // Piecewise-constant tempo over musical time. Converts between ticks and
    // seconds / samples. Used for timeline math and (later) offline rendering;
    // the live transport only needs the current tempo.
    class TempoMap
    {
    public:
        struct Change
        {
            Tick tick = 0;
            double bpm = defaultTempo;
        };

        explicit TempoMap (double initialBpm = defaultTempo);

        // Replaces any change at the same tick. Returns an error message if the
        // change is invalid (negative tick, tempo out of range).
        std::optional<std::string> addChange (Tick, double bpm);

        double tempoAt (Tick) const noexcept;
        const std::vector<Change>& getChanges() const noexcept { return changes; }

        double ticksToSeconds (double ticks) const noexcept;
        double secondsToTicks (double seconds) const noexcept;

        double ticksToSamples (double ticks, double sampleRate) const noexcept  { return ticksToSeconds (ticks) * sampleRate; }
        double samplesToTicks (double samples, double sampleRate) const noexcept { return secondsToTicks (samples / sampleRate); }

        static double secondsPerTick (double bpm) noexcept { return 60.0 / (bpm * (double) ppq); }

    private:
        std::vector<Change> changes;   // sorted, first at tick 0
    };
}
