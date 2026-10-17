#include "beatforge/audio/AudioEngine.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace beatforge::audio
{
    namespace
    {
        std::uint32_t pack (time::TimeSignature s) noexcept   { return (std::uint32_t) s.numerator << 8 | (std::uint32_t) s.denominator; }
        time::TimeSignature unpack (std::uint32_t v) noexcept  { return { (int) (v >> 8), (int) (v & 0xff) }; }
    }

    AudioEngine::AudioEngine()
        : packedSignature (pack ({}))
    {
        static_assert (std::atomic<double>::is_always_lock_free);
        static_assert (std::atomic<time::Tick>::is_always_lock_free);
    }

    void AudioEngine::prepare (double newSampleRate) noexcept
    {
        transport.prepare (newSampleRate);
        metronome.prepare (newSampleRate);
        sampleRate.store (newSampleRate, std::memory_order_release);
    }

    void AudioEngine::reset() noexcept
    {
        playRequested.store (false, std::memory_order_release);
        transport.rewind();
        metronome.reset();
        publishPosition();
        state.store (TransportState::stopped, std::memory_order_release);
    }

    void AudioEngine::setTempo (double bpm) noexcept
    {
        tempo.store (time::clampTempo (bpm), std::memory_order_release);
    }

    void AudioEngine::setTimeSignature (time::TimeSignature sig) noexcept
    {
        if (sig.isValid())
            packedSignature.store (pack (sig), std::memory_order_release);
    }

    time::TimeSignature AudioEngine::getTimeSignature() const noexcept
    {
        return unpack (packedSignature.load (std::memory_order_acquire));
    }

    void AudioEngine::setMetronomeEnabled (bool enabled) noexcept
    {
        metronomeEnabled.store (enabled, std::memory_order_release);
    }

    void AudioEngine::setLoop (bool enabled, time::Tick start, time::Tick end) noexcept
    {
        // Disable first so the audio thread never pairs a new start with an old end.
        loopEnabled.store (false, std::memory_order_release);
        loopStart.store (start, std::memory_order_release);
        loopEnd.store (end, std::memory_order_release);
        loopEnabled.store (enabled, std::memory_order_release);
    }

    void AudioEngine::applySettings() noexcept
    {
        transport.setTempo (tempo.load (std::memory_order_acquire));
        transport.setTimeSignature (getTimeSignature());

        const bool loop = loopEnabled.load (std::memory_order_acquire);
        transport.setLoop (loop, loopStart.load (std::memory_order_acquire), loopEnd.load (std::memory_order_acquire));
    }

    void AudioEngine::publishPosition() noexcept
    {
        positionTicks.store (transport.getPositionTicks(), std::memory_order_release);
        positionSamples.store (transport.getElapsedSamples(), std::memory_order_release);
    }

    void AudioEngine::process (float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                std::memset (outputs[ch], 0, sizeof (float) * (size_t) numSamples);

        // Sound goes to the first stereo pair; extra outputs stay silent.
        float* pair[2] {};
        int numPair = 0;
        for (int ch = 0; ch < numOutputs && numPair < 2; ++ch)
            if (outputs[ch] != nullptr)
                pair[numPair++] = outputs[ch];

        applySettings();

        const bool wantPlay = playRequested.load (std::memory_order_acquire);

        if (wantPlay)
        {
            // Collect this block's beats, then render the click voice between them
            // so each click starts on its exact sample.
            std::array<time::Transport::Beat, maxBeatsPerBlock> beats;
            int numBeats = 0;

            transport.advance (numSamples, [&] (const time::Transport::Beat& b)
            {
                if (numBeats < maxBeatsPerBlock)
                    beats[(size_t) numBeats++] = b;
            });

            const bool clicks = metronomeEnabled.load (std::memory_order_acquire);
            int rendered = 0;

            for (int i = 0; i < numBeats; ++i)
            {
                metronome.render (pair, numPair, rendered, beats[(size_t) i].sampleOffset - rendered);
                rendered = beats[(size_t) i].sampleOffset;

                if (clicks)
                    metronome.trigger (beats[(size_t) i].isDownbeat());
            }

            metronome.render (pair, numPair, rendered, numSamples - rendered);
            beatCount.fetch_add ((std::uint64_t) numBeats, std::memory_order_relaxed);
            publishPosition();
            state.store (TransportState::playing, std::memory_order_release);
        }
        else
        {
            // Let a ringing click finish instead of cutting it off.
            metronome.render (pair, numPair, 0, numSamples);

            if (metronome.isSounding())
            {
                state.store (TransportState::stopping, std::memory_order_release);
            }
            else if (state.load (std::memory_order_relaxed) != TransportState::stopped)
            {
                transport.rewind();
                publishPosition();
                state.store (TransportState::stopped, std::memory_order_release);
            }
        }

        outputMeter.process (outputs, std::min (numOutputs, LevelMeter::maxChannels), numSamples);
    }
}
