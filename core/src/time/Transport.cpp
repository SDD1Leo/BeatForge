#include "beatforge/time/Transport.h"

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
        lastBeatTick = -1;
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
