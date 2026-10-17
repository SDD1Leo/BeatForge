#pragma once

#include "beatforge/audio/LevelMeter.h"
#include "beatforge/audio/Metronome.h"
#include "beatforge/time/Transport.h"

#include <atomic>
#include <cstdint>

namespace beatforge::audio
{
    enum class TransportState
    {
        stopped,
        playing,
        stopping   // stop requested; the last click is still ringing out
    };

    // Everything the audio callback runs. Device-agnostic: the app feeds it
    // from a JUCE device callback, tests feed it plain buffers.
    //
    // Threading:
    //  - prepare()/reset() only while no process() call can run
    //    (device stopped / about to start).
    //  - Setters and getters may be called from any thread; settings reach
    //    the audio thread through atomics and apply at the next block.
    //  - process() is the audio thread; it never locks or allocates.
    class AudioEngine
    {
    public:
        AudioEngine();

        void prepare (double sampleRate) noexcept;

        // Silences immediately and rewinds; for when the device has gone away.
        void reset() noexcept;

        void play() noexcept  { playRequested.store (true, std::memory_order_release); }
        void stop() noexcept  { playRequested.store (false, std::memory_order_release); }

        void setTempo (double bpm) noexcept;
        void setTimeSignature (time::TimeSignature) noexcept;
        void setMetronomeEnabled (bool) noexcept;
        void setLoop (bool enabled, time::Tick start, time::Tick end) noexcept;

        // Writes the output for one block into every output channel.
        void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

        TransportState getState() const noexcept  { return state.load (std::memory_order_acquire); }
        bool isPlayRequested() const noexcept     { return playRequested.load (std::memory_order_acquire); }

        double getTempo() const noexcept                       { return tempo.load (std::memory_order_acquire); }
        time::TimeSignature getTimeSignature() const noexcept;
        bool isMetronomeEnabled() const noexcept               { return metronomeEnabled.load (std::memory_order_acquire); }

        // Musical position (ticks) and samples rendered since Play; both return
        // to 0 once a stop completes.
        double getPositionTicks() const noexcept          { return positionTicks.load (std::memory_order_acquire); }
        std::int64_t getPositionSamples() const noexcept  { return positionSamples.load (std::memory_order_acquire); }
        double getSampleRate() const noexcept             { return sampleRate.load (std::memory_order_acquire); }

        // Total beats the transport has fired since construction (diagnostics/tests).
        std::uint64_t getBeatCount() const noexcept { return beatCount.load (std::memory_order_relaxed); }

        LevelMeter& getOutputMeter() noexcept { return outputMeter; }

        static constexpr int maxBeatsPerBlock = 64;

    private:
        void applySettings() noexcept;
        void publishPosition() noexcept;

        // Audio-thread state.
        time::Transport transport;
        Metronome metronome;
        LevelMeter outputMeter;

        // Settings written by any thread, read by the audio thread.
        std::atomic<bool> playRequested { false };
        std::atomic<double> tempo { time::defaultTempo };
        std::atomic<std::uint32_t> packedSignature;   // numerator << 8 | denominator
        std::atomic<bool> metronomeEnabled { true };
        std::atomic<bool> loopEnabled { false };
        std::atomic<time::Tick> loopStart { 0 }, loopEnd { 0 };

        // Published by the audio thread.
        std::atomic<TransportState> state { TransportState::stopped };
        std::atomic<double> positionTicks { 0.0 };
        std::atomic<std::int64_t> positionSamples { 0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<std::uint64_t> beatCount { 0 };
    };
}
