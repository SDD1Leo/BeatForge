#pragma once

#include "beatforge/audio/DrumMachine.h"
#include "beatforge/audio/LevelMeter.h"
#include "beatforge/audio/Metronome.h"
#include "beatforge/audio/Instruments.h"
#include "beatforge/audio/PreviewQueue.h"
#include "beatforge/midi/NoteSchedule.h"
#include "beatforge/time/Transport.h"

#include <atomic>
#include <cstdint>

namespace beatforge::audio
{
    // Stop rewinds immediately; sounds that are still ringing finish on their own.
    enum class TransportState
    {
        stopped,
        playing
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

        // --- MIDI clip (message thread) ---------------------------------------
        // Replaces what the synth track plays; nullptr = no clip. Builds the
        // schedule here and hands it to the audio thread lock-free.
        void setMidiClip (const midi::Clip*);
        // Frees schedules the audio thread has finished with. Call regularly.
        void collectGarbage();

        // Editor auditions (any thread except audio): note on until note off.
        void previewNoteOn (int pitch, int velocity) noexcept  { previews.push ({ pitch, velocity }); }
        void previewNoteOff (int pitch) noexcept               { previews.push ({ pitch, 0 }); }

        // Writes the output for one block into every output channel.
        void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

        TransportState getState() const noexcept  { return state.load (std::memory_order_acquire); }
        bool isPlayRequested() const noexcept     { return playRequested.load (std::memory_order_acquire); }

        double getTempo() const noexcept                       { return tempo.load (std::memory_order_acquire); }
        time::TimeSignature getTimeSignature() const noexcept;
        bool isMetronomeEnabled() const noexcept               { return metronomeEnabled.load (std::memory_order_acquire); }
        bool isLoopEnabled() const noexcept                    { return loopEnabled.load (std::memory_order_acquire); }
        time::Tick getLoopEnd() const noexcept                 { return loopEnd.load (std::memory_order_acquire); }

        // Musical position (ticks) and samples rendered since Play; both return
        // to 0 once a stop completes.
        double getPositionTicks() const noexcept          { return positionTicks.load (std::memory_order_acquire); }
        std::int64_t getPositionSamples() const noexcept  { return positionSamples.load (std::memory_order_acquire); }
        double getSampleRate() const noexcept             { return sampleRate.load (std::memory_order_acquire); }

        // Total beats the transport has fired since construction (diagnostics/tests).
        std::uint64_t getBeatCount() const noexcept { return beatCount.load (std::memory_order_relaxed); }

        LevelMeter& getOutputMeter() noexcept { return outputMeter; }
        DrumMachine& getDrumMachine() noexcept { return drums; }

        int getActiveSynthVoices() const noexcept { return activeSynthVoices.load (std::memory_order_relaxed); }

        // The synth track's instrument and its parameters (any thread).
        InstrumentRack& getInstruments() noexcept { return instruments; }

        static constexpr int maxGridLinesPerBlock = 64;
        static constexpr int maxEventsPerBlock = 256;

    private:
        void applySettings() noexcept;
        void publishPosition() noexcept;
        void renderVoices (float* const* outputs, int numOutputs, int start, int num) noexcept;
        void releaseNotesNotCovering (double absoluteTick) noexcept;

        // Audio-thread state.
        time::Transport transport;
        Metronome metronome;
        DrumMachine drums;
        InstrumentRack instruments;
        LevelMeter outputMeter;
        midi::ScheduleExchange schedules;
        const midi::NoteSchedule* schedule = nullptr;   // audio thread
        PreviewQueue previews;

        // Settings written by any thread, read by the audio thread.
        std::atomic<bool> playRequested { false };
        std::atomic<double> tempo { time::defaultTempo };
        std::atomic<std::uint32_t> packedSignature;   // numerator << 8 | denominator
        std::atomic<bool> metronomeEnabled { false };
        std::atomic<bool> loopEnabled { false };
        std::atomic<time::Tick> loopStart { 0 }, loopEnd { 0 };

        // Published by the audio thread.
        std::atomic<TransportState> state { TransportState::stopped };
        std::atomic<double> positionTicks { 0.0 };
        std::atomic<std::int64_t> positionSamples { 0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<std::uint64_t> beatCount { 0 };
        std::atomic<int> activeSynthVoices { 0 };
    };
}
