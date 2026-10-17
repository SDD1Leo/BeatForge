#pragma once

#include "beatforge/audio/LevelMeter.h"
#include "beatforge/audio/TestTone.h"

#include <atomic>
#include <cstdint>

namespace beatforge::audio
{
    enum class TransportState
    {
        stopped,
        playing,
        stopping   // stop requested; output is fading to silence
    };

    // Everything the audio callback runs. Device-agnostic: the app feeds it
    // from a JUCE device callback, tests feed it plain buffers.
    //
    // Threading:
    //  - prepare()/reset() only while no process() call can run
    //    (device stopped / about to start).
    //  - play()/stop() and the getters may be called from any thread.
    //  - process() is the audio thread; it never locks or allocates.
    class AudioEngine
    {
    public:
        void prepare (double sampleRate) noexcept;

        // Silences immediately and rewinds; for when the device has gone away.
        void reset() noexcept;

        void play() noexcept  { playRequested.store (true, std::memory_order_release); }
        void stop() noexcept  { playRequested.store (false, std::memory_order_release); }

        // Writes the output for one block into every output channel.
        void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

        TransportState getState() const noexcept  { return state.load (std::memory_order_acquire); }
        bool isPlayRequested() const noexcept     { return playRequested.load (std::memory_order_acquire); }

        // Samples rendered since Play; returns to 0 once a stop completes.
        std::int64_t getPositionSamples() const noexcept { return positionSamples.load (std::memory_order_acquire); }
        double getSampleRate() const noexcept            { return sampleRate.load (std::memory_order_acquire); }
        std::uint64_t getProcessedBlockCount() const noexcept { return blocksProcessed.load (std::memory_order_relaxed); }

        LevelMeter& getOutputMeter() noexcept { return outputMeter; }

    private:
        TestTone tone;
        LevelMeter outputMeter;

        std::atomic<bool> playRequested { false };
        std::atomic<TransportState> state { TransportState::stopped };
        std::atomic<std::int64_t> positionSamples { 0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<std::uint64_t> blocksProcessed { 0 };
    };
}
