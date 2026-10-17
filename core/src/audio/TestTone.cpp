#include "beatforge/audio/TestTone.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beatforge::audio
{
    void TestTone::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        phaseIncrement = defaultFrequencyHz / sampleRate;
        peakGain = std::pow (10.0f, defaultGainDb / 20.0f);
        rampStep = static_cast<float> (1.0 / std::max (1.0, rampSeconds * sampleRate));
        phase = 0.0;
        currentGain = 0.0f;
    }

    void TestTone::setActive (bool shouldBeActive) noexcept
    {
        // Restart the waveform from zero phase when starting from silence so
        // every start is identical.
        if (shouldBeActive && ! active && currentGain == 0.0f)
            phase = 0.0;

        active = shouldBeActive;
    }

    bool TestTone::isSounding() const noexcept
    {
        return active || currentGain > 0.0f;
    }

    void TestTone::render (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (numChannels <= 0 || numSamples <= 0)
            return;

        const float target = active ? 1.0f : 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            if (currentGain < target)
                currentGain = std::min (target, currentGain + rampStep);
            else if (currentGain > target)
                currentGain = std::max (target, currentGain - rampStep);

            float sample = 0.0f;

            if (currentGain > 0.0f)
            {
                sample = peakGain * currentGain
                       * static_cast<float> (std::sin (2.0 * std::numbers::pi * phase));
                phase += phaseIncrement;
                phase -= std::floor (phase);
            }

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] = sample;
        }
    }
}
