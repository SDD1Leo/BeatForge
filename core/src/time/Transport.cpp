#include "beatforge/time/Transport.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace beatforge::time
{
    void Transport::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        samplesPerTick = TempoMap::secondsPerTick (bpm) * sampleRate;
        rewind();
    }

    void Transport::rewind() noexcept
    {
        now = 0;
        reanchor (0, 0.0);
        lastLineTick = -1;
        jumpPending = false;
        eventBoundary = -0.5;
    }

    void Transport::setTempo (double newBpm) noexcept
    {
        newBpm = clampTempo (newBpm);

        if (newBpm == bpm && samplesPerTick > 0.0)
            return;

        reanchor (now, tickAt (now));
        bpm = newBpm;
        samplesPerTick = TempoMap::secondsPerTick (bpm) * sampleRate;
    }

    void Transport::seekTo (double tick) noexcept
    {
        tick = std::max (0.0, tick);
        reanchor (now, tick);
        lastLineTick = (Tick) std::ceil (tick) - 1;
        eventBoundary = std::ceil (tick) - 0.5;
        jumpPending = true;
    }

    Tick Transport::gridTicks() const noexcept
    {
        return std::gcd (stepTicks, signature.ticksPerBeat());
    }

    void Transport::setTimeSignature (TimeSignature sig) noexcept
    {
        if (sig.isValid())
            signature = sig;
    }

    void Transport::setLoop (bool enabled, Tick start, Tick end) noexcept
    {
        loopEnabled = enabled;
        loopStart = start;
        loopEnd = end;
    }
}
