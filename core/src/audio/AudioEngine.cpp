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
        drums.prepare (newSampleRate);   // allocates the kit; never runs alongside process()
        sampleRate.store (newSampleRate, std::memory_order_release);
    }

    void AudioEngine::reset() noexcept
    {
        playRequested.store (false, std::memory_order_release);
        transport.rewind();
        metronome.reset();
        drums.reset();
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

    void AudioEngine::renderVoices (float* const* outputs, int numOutputs, int start, int num) noexcept
    {
        if (num <= 0)
            return;

        metronome.render (outputs, numOutputs, start, num);
        drums.render (outputs, numOutputs, start, num);
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
        drums.beginBlock();
        drums.startPendingPreviews();

        const bool wantPlay = playRequested.load (std::memory_order_acquire);

        if (wantPlay)
        {
            // Collect this block's grid lines, then render between them so every
            // click and drum hit starts on its exact sample.
            std::array<time::Transport::GridLine, maxGridLinesPerBlock> lines;
            int numLines = 0;

            transport.advance (numSamples, [&] (const time::Transport::GridLine& line)
            {
                if (numLines < maxGridLinesPerBlock)
                    lines[(size_t) numLines++] = line;
            });

            const bool clicks = metronomeEnabled.load (std::memory_order_acquire);
            int rendered = 0;
            std::uint64_t beats = 0;

            for (int i = 0; i < numLines; ++i)
            {
                const auto& line = lines[(size_t) i];
                renderVoices (pair, numPair, rendered, line.sampleOffset - rendered);
                rendered = line.sampleOffset;

                if (line.isBeat)
                {
                    ++beats;
                    if (clicks)
                        metronome.trigger (line.isDownbeat());
                }

                if (line.isStep)
                    drums.triggerStep ((int) ((line.tick / time::Transport::stepTicks) % DrumMachine::numSteps));
            }

            renderVoices (pair, numPair, rendered, numSamples - rendered);
            beatCount.fetch_add (beats, std::memory_order_relaxed);
            publishPosition();
            state.store (TransportState::playing, std::memory_order_release);
        }
        else
        {
            // Previews and ringing tails keep sounding while stopped.
            renderVoices (pair, numPair, 0, numSamples);

            if (state.load (std::memory_order_relaxed) != TransportState::stopped)
            {
                transport.rewind();
                publishPosition();
                state.store (TransportState::stopped, std::memory_order_release);
            }
        }

        outputMeter.process (outputs, std::min (numOutputs, LevelMeter::maxChannels), numSamples);
    }
}
