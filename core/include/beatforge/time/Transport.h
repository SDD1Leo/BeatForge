#pragma once

#include "beatforge/time/MusicalTime.h"
#include "beatforge/time/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace beatforge::time
{
    // The musical clock. Owned and advanced by the audio thread only.
    //
    // Position is derived, never accumulated: tick(s) = anchorTick +
    // (s - anchorSample) / samplesPerTick, where s is an integer sample count.
    // The anchor moves only on tempo changes and loop wraps, so long playback
    // does not drift.
    //
    // A musical event at tick T fires at the first sample s with tick(s) >= T,
    // i.e. s = ceil(sampleFor(T)). Each beat fires exactly once, whatever the
    // block size.
    class Transport
    {
    public:
        struct Beat
        {
            int sampleOffset = 0;   // within the block passed to advance()
            Tick tick = 0;
            int beatInBar = 1;      // 1-based
            bool isDownbeat() const noexcept { return beatInBar == 1; }
        };

        void prepare (double sampleRate) noexcept;

        // Back to tick 0 at sample 0.
        void rewind() noexcept;

        // Applied from the current sample onwards; earlier beats keep their timing.
        void setTempo (double bpm) noexcept;
        void setTimeSignature (TimeSignature) noexcept;

        // Loop [start, end) in ticks. Ignored unless 0 <= start < end.
        void setLoop (bool enabled, Tick start, Tick end) noexcept;

        // Advances the clock by numSamples, calling onBeat (const Beat&) for every
        // beat that falls inside this block, in order.
        template <typename OnBeat>
        void advance (int numSamples, OnBeat&& onBeat) noexcept;

        double getPositionTicks() const noexcept      { return tickAt (now); }
        std::int64_t getElapsedSamples() const noexcept { return now; }
        double getTempo() const noexcept               { return bpm; }
        TimeSignature getTimeSignature() const noexcept { return signature; }

    private:
        double tickAt (std::int64_t sample) const noexcept
        {
            return anchorTick + (double) (sample - anchorSample) / samplesPerTick;
        }

        std::int64_t firstSampleAtOrAfter (double tick) const noexcept
        {
            // Small tolerance so exact grid hits are not pushed a sample late
            // by floating-point error.
            return anchorSample + (std::int64_t) std::ceil ((tick - anchorTick) * samplesPerTick - 1e-7);
        }

        bool loopActive() const noexcept { return loopEnabled && loopStart >= 0 && loopEnd > loopStart; }

        template <typename OnBeat>
        void emitBeats (std::int64_t from, std::int64_t to, std::int64_t blockStart, OnBeat& onBeat) noexcept;

        void reanchor (std::int64_t sample, double tick) noexcept
        {
            anchorSample = sample;
            anchorTick = tick;
        }

        double sampleRate = 48000.0;
        double bpm = defaultTempo;
        double samplesPerTick = 0.0;
        TimeSignature signature;

        std::int64_t now = 0;           // samples since rewind
        std::int64_t anchorSample = 0;
        double anchorTick = 0.0;
        Tick lastBeatTick = -1;         // de-duplicates beats across tempo changes

        bool loopEnabled = false;
        Tick loopStart = 0, loopEnd = 0;
    };

    template <typename OnBeat>
    void Transport::emitBeats (std::int64_t from, std::int64_t to, std::int64_t blockStart, OnBeat& onBeat) noexcept
    {
        const auto perBeat = signature.ticksPerBeat();
        const auto startTick = tickAt (from);
        auto beatTick = (Tick) std::floor (startTick / (double) perBeat) * perBeat;

        for (;; beatTick += perBeat)
        {
            if (beatTick <= lastBeatTick)
                continue;

            const auto s = firstSampleAtOrAfter ((double) beatTick);

            if (s >= to)
                break;

            if (s < from)
                continue;

            lastBeatTick = beatTick;
            const auto bbt = toBarBeatTick (beatTick, signature);
            onBeat (Beat { (int) (s - blockStart), beatTick, bbt.beat });
        }
    }

    template <typename OnBeat>
    void Transport::advance (int numSamples, OnBeat&& onBeat) noexcept
    {
        const auto blockStart = now;
        const auto blockEnd = now + numSamples;

        while (now < blockEnd)
        {
            auto segmentEnd = blockEnd;
            bool wraps = false;

            if (loopActive() && tickAt (now) < (double) loopEnd)
            {
                const auto loopEndSample = firstSampleAtOrAfter ((double) loopEnd);

                // <= so a loop end landing exactly on the block boundary still
                // wraps; otherwise the next block starts past the loop.
                if (loopEndSample <= segmentEnd)
                {
                    segmentEnd = std::max (loopEndSample, now);
                    wraps = true;
                }
            }

            emitBeats (now, segmentEnd, blockStart, onBeat);
            now = segmentEnd;

            if (wraps)
            {
                reanchor (now, (double) loopStart);
                lastBeatTick = loopStart - 1;
            }
        }
    }
}
