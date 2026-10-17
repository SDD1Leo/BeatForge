#include "beatforge/midi/Note.h"

#include <algorithm>
#include <cmath>

namespace beatforge::midi
{
    bool isValidGrid (time::Tick grid) noexcept
    {
        return grid == quarter || grid == eighth || grid == sixteenth || grid == thirtySecond;
    }

    Note constrained (Note n, time::Tick clipLength) noexcept
    {
        n.pitch = std::clamp (n.pitch, minPitch, maxPitch);
        n.velocity = std::clamp (n.velocity, minVelocity, maxVelocity);
        n.start = std::clamp<time::Tick> (n.start, 0, std::max<time::Tick> (0, clipLength - 1));
        n.length = std::clamp<time::Tick> (n.length, 1, clipLength - n.start);
        return n;
    }

    void quantize (std::vector<Note>& notes, time::Tick grid, time::Tick clipLength, const std::vector<NoteId>& ids)
    {
        if (grid <= 0)
            return;

        for (auto& n : notes)
        {
            if (! ids.empty() && std::find (ids.begin(), ids.end(), n.id) == ids.end())
                continue;

            auto snapped = ((n.start + grid / 2) / grid) * grid;
            if (snapped >= clipLength)
                snapped = ((clipLength - 1) / grid) * grid;   // last grid line inside the clip

            n.start = snapped;
            n = constrained (n, clipLength);
        }
    }

    double pitchToFrequency (int pitch) noexcept
    {
        return 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    }
}
