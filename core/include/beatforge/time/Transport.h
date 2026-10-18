#pragma once

#include "beatforge/time/MusicalTime.h"
#include "beatforge/time/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

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
    // i.e. s = ceil(sampleFor(T)). Each grid line fires exactly once, whatever
    // the block size.
    //
    // The grid is every 16th note (sequencer step) and every beat; with the
    // usual time signatures beats are a subset of the 16th-note lines.
    class Transport
    {
    public:
        static constexpr Tick stepTicks = ppq / 4;   // one 16th note

        struct GridLine
        {
            int sampleOffset = 0;   // within the block passed to advance()
            Tick tick = 0;
            bool isStep = false;    // on the 16th-note grid
            bool isBeat = false;    // on a beat of the time signature
            int beatInBar = 0;      // 1-based when isBeat
            bool isDownbeat() const noexcept { return isBeat && beatInBar == 1; }
        };

        void prepare (double sampleRate) noexcept;

        // Back to tick 0 at sample 0.
        void rewind() noexcept;

        // Continues from `tick` at the current sample. Events at exactly `tick`
        // (grid lines, notes) fire on the next sample rendered.
        void seekTo (double tick) noexcept;

        // Applied from the current sample onwards; earlier beats keep their timing.
        void setTempo (double bpm) noexcept;
        void setTimeSignature (TimeSignature) noexcept;

        // Loop [start, end) in ticks. Ignored unless 0 <= start < end.
        void setLoop (bool enabled, Tick start, Tick end) noexcept;

        // A stretch of the block with continuous musical time. Blocks split into
        // several segments only at loop wraps.
        //
        // Events: an event at tick T belongs to this segment iff
        // eventsAfter < T <= eventsUpTo. Those bounds are chosen so that exactly
        // the events whose firing sample (first sample at or after T) lies in this
        // segment are included, and consecutive segments share their bound, so
        // no event is lost or repeated - also across tempo changes and blocks.
        struct Segment
        {
            int sampleOffset = 0;      // within the block
            int numSamples = 0;
            double startTick = 0.0;    // musical position of the first sample
            double endTick = 0.0;      // position after the last sample
            double eventsAfter = 0.0;  // exclusive
            double eventsUpTo = 0.0;   // inclusive
            double samplesPerTick = 1.0;
            bool afterJump = false;    // starts right after a loop wrap

            // Block sample offset at which an event at `tick` (inside the segment) fires.
            int sampleFor (double tick) const noexcept
            {
                const auto s = sampleOffset + (int) std::ceil ((tick - startTick) * samplesPerTick - 1e-7);
                return std::clamp (s, sampleOffset, sampleOffset + numSamples - 1);
            }
        };

        // Advances the clock by numSamples, calling onLine (const GridLine&) for
        // every grid line that falls inside this block, in order.
        template <typename OnLine>
        void advance (int numSamples, OnLine&& onLine) noexcept
        {
            advance (numSamples, std::forward<OnLine> (onLine), [] (const Segment&) {});
        }

        // As above, also calling onSegment (const Segment&) for each segment.
        template <typename OnLine, typename OnSegment>
        void advance (int numSamples, OnLine&& onLine, OnSegment&& onSegment) noexcept;

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

        Tick gridTicks() const noexcept;

        template <typename OnLine>
        void emitGridLines (std::int64_t from, std::int64_t to, std::int64_t blockStart, OnLine& onLine) noexcept;

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
        Tick lastLineTick = -1;         // de-duplicates grid lines across tempo changes
        double eventBoundary = -0.5;    // events up to here belong to earlier segments
        bool jumpPending = false;       // a loop wrap happened; the next segment reports it

        bool loopEnabled = false;
        Tick loopStart = 0, loopEnd = 0;
    };

    template <typename OnLine>
    void Transport::emitGridLines (std::int64_t from, std::int64_t to, std::int64_t blockStart, OnLine& onLine) noexcept
    {
        const auto grid = gridTicks();
        const auto perBeat = signature.ticksPerBeat();
        auto lineTick = (Tick) std::floor (tickAt (from) / (double) grid) * grid;

        for (;; lineTick += grid)
        {
            if (lineTick <= lastLineTick)
                continue;

            const auto s = firstSampleAtOrAfter ((double) lineTick);

            if (s >= to)
                break;

            if (s < from)
                continue;

            lastLineTick = lineTick;

            GridLine line;
            line.sampleOffset = (int) (s - blockStart);
            line.tick = lineTick;
            line.isStep = lineTick % stepTicks == 0;
            line.isBeat = lineTick % perBeat == 0;
            line.beatInBar = line.isBeat ? toBarBeatTick (lineTick, signature).beat : 0;
            onLine (line);
        }
    }

    template <typename OnLine, typename OnSegment>
    void Transport::advance (int numSamples, OnLine&& onLine, OnSegment&& onSegment) noexcept
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

            emitGridLines (now, segmentEnd, blockStart, onLine);

            if (segmentEnd > now)
            {
                Segment segment;
                segment.sampleOffset = (int) (now - blockStart);
                segment.numSamples = (int) (segmentEnd - now);
                segment.startTick = tickAt (now);
                segment.endTick = wraps ? (double) loopEnd : tickAt (segmentEnd);
                segment.samplesPerTick = samplesPerTick;

                // Last tick that still fires on or before this segment's last sample
                // (inverse of firstSampleAtOrAfter). Before a wrap, nothing at or
                // after the loop end plays (ticks are whole numbers).
                const auto lastTickHere = anchorTick + ((double) (segmentEnd - 1 - anchorSample) + 1e-7) / samplesPerTick;
                segment.eventsAfter = eventBoundary;
                segment.eventsUpTo = wraps ? std::min (lastTickHere, (double) loopEnd - 0.5) : lastTickHere;
                eventBoundary = segment.eventsUpTo;
                segment.afterJump = jumpPending;
                jumpPending = false;
                onSegment (segment);
            }

            now = segmentEnd;

            if (wraps)
            {
                reanchor (now, (double) loopStart);
                lastLineTick = loopStart - 1;
                eventBoundary = (double) loopStart - 0.5;
                jumpPending = true;   // flags the next segment, even if it is in the next block
            }
        }
    }
}
