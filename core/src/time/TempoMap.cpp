#include "beatforge/time/TempoMap.h"

#include <algorithm>

namespace beatforge::time
{
    double clampTempo (double bpm) noexcept
    {
        return std::clamp (bpm, minTempo, maxTempo);
    }

    TempoMap::TempoMap (double initialBpm)
        : changes { { 0, clampTempo (initialBpm) } }
    {
    }

    std::optional<std::string> TempoMap::addChange (Tick tick, double bpm)
    {
        if (tick < 0)
            return "tempo change position must not be negative";

        if (! (bpm >= minTempo && bpm <= maxTempo))
            return "tempo must be between 40 and 240 BPM";

        auto it = std::lower_bound (changes.begin(), changes.end(), tick,
                                    [] (const Change& c, Tick t) { return c.tick < t; });

        if (it != changes.end() && it->tick == tick)
            it->bpm = bpm;
        else
            changes.insert (it, { tick, bpm });

        return std::nullopt;
    }

    double TempoMap::tempoAt (Tick tick) const noexcept
    {
        auto it = std::upper_bound (changes.begin(), changes.end(), tick,
                                    [] (Tick t, const Change& c) { return t < c.tick; });
        return std::prev (it)->bpm;
    }

    double TempoMap::ticksToSeconds (double ticks) const noexcept
    {
        double seconds = 0.0;

        for (size_t i = 0; i < changes.size(); ++i)
        {
            const double segStart = (double) changes[i].tick;
            const double segEnd = i + 1 < changes.size() ? (double) changes[i + 1].tick : ticks;

            if (ticks <= segStart)
                break;

            seconds += (std::min (ticks, segEnd) - segStart) * secondsPerTick (changes[i].bpm);
        }

        // Before tick 0, extrapolate with the first tempo.
        if (ticks < 0.0)
            seconds = ticks * secondsPerTick (changes.front().bpm);

        return seconds;
    }

    double TempoMap::secondsToTicks (double seconds) const noexcept
    {
        if (seconds < 0.0)
            return seconds / secondsPerTick (changes.front().bpm);

        double elapsed = 0.0;

        for (size_t i = 0; i < changes.size(); ++i)
        {
            const auto spt = secondsPerTick (changes[i].bpm);

            if (i + 1 < changes.size())
            {
                const auto segSeconds = (double) (changes[i + 1].tick - changes[i].tick) * spt;

                if (seconds < elapsed + segSeconds)
                    return (double) changes[i].tick + (seconds - elapsed) / spt;

                elapsed += segSeconds;
            }
            else
            {
                return (double) changes[i].tick + (seconds - elapsed) / spt;
            }
        }

        return 0.0;
    }
}
