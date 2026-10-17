#include "beatforge/audio/Metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beatforge::audio
{
    void Metronome::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        length = std::max (1, (int) (clickSeconds * sampleRate));
        fadeLength = std::max (1, (int) (0.002 * sampleRate));            // 2 ms tail fade
        decayPerSample = (float) std::exp (-1.0 / (0.006 * sampleRate));  // 6 ms time constant
        remaining = 0;
    }

    void Metronome::trigger (bool accent) noexcept
    {
        phase = 0.0;
        phaseIncrement = (accent ? accentHz : normalHz) / sampleRate;
        gain = accent ? accentGain : normalGain;
        envelope = 1.0f;
        remaining = length;
    }

    void Metronome::render (float* const* channels, int numChannels, int start, int num) noexcept
    {
        for (int i = start; i < start + num && remaining > 0; ++i, --remaining)
        {
            const float fade = remaining < fadeLength ? (float) remaining / (float) fadeLength : 1.0f;
            const float sample = gain * envelope * fade * (float) std::sin (2.0 * std::numbers::pi * phase);

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] += sample;

            phase += phaseIncrement;
            phase -= std::floor (phase);
            envelope *= decayPerSample;
        }
    }
}
