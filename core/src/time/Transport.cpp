#include "beatforge/time/Transport.h"

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
