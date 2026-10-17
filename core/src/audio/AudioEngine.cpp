#include "beatforge/audio/AudioEngine.h"

#include <algorithm>
#include <cstring>

namespace beatforge::audio
{
    void AudioEngine::prepare (double newSampleRate) noexcept
    {
        tone.prepare (newSampleRate);
        sampleRate.store (newSampleRate, std::memory_order_release);
    }

    void AudioEngine::reset() noexcept
    {
        playRequested.store (false, std::memory_order_release);
        tone.prepare (tone.getSampleRate());
        positionSamples.store (0, std::memory_order_release);
        state.store (TransportState::stopped, std::memory_order_release);
    }

    void AudioEngine::process (float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        blocksProcessed.fetch_add (1, std::memory_order_relaxed);

        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                std::memset (outputs[ch], 0, sizeof (float) * (size_t) numSamples);

        const bool wantPlay = playRequested.load (std::memory_order_acquire);
        tone.setActive (wantPlay);

        if (tone.isSounding())
        {
            // The tone goes to the first stereo pair; extra outputs stay silent.
            float* pair[2] {};
            int numPair = 0;

            for (int ch = 0; ch < numOutputs && numPair < 2; ++ch)
                if (outputs[ch] != nullptr)
                    pair[numPair++] = outputs[ch];

            tone.render (pair, numPair, numSamples);
        }

        if (wantPlay)
        {
            positionSamples.fetch_add (numSamples, std::memory_order_acq_rel);
            state.store (TransportState::playing, std::memory_order_release);
        }
        else if (tone.isSounding())
        {
            state.store (TransportState::stopping, std::memory_order_release);
        }
        else if (state.load (std::memory_order_relaxed) != TransportState::stopped)
        {
            positionSamples.store (0, std::memory_order_release);
            state.store (TransportState::stopped, std::memory_order_release);
        }

        outputMeter.process (outputs, std::min (numOutputs, LevelMeter::maxChannels), numSamples);
    }
}
