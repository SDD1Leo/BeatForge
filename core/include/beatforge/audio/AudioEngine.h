#pragma once

#include "beatforge/audio/DrumMachine.h"
#include "beatforge/audio/Instruments.h"
#include "beatforge/audio/LevelMeter.h"
#include "beatforge/audio/Metronome.h"
#include "beatforge/audio/PreviewQueue.h"
#include "beatforge/audio/TrackSchedule.h"
#include "beatforge/time/Transport.h"
#include "beatforge/util/LockFreeExchange.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace beatforge::audio
{
    // Stop rewinds immediately; sounds that are still ringing finish on their own.
    enum class TransportState
    {
        stopped,
        playing
    };

    enum class SlotType { none, drums, instrument };

    // Everything the audio callback runs. Device-agnostic: the app feeds it
    // from a JUCE device callback, tests feed it plain buffers.
    //
    // The arrangement is up to `maxTracks` track slots. Each slot has a drum
    // machine and an instrument rack (its type decides which plays), a
    // schedule of events on the song timeline, and volume/pan/mute/solo.
    //
    // Threading:
    //  - prepare()/reset() only while no process() call can run.
    //  - Setters may be called from any thread except the audio thread;
    //    settings reach the audio thread through atomics and lock-free
    //    hand-overs and apply at the next block.
    //  - process() is the audio thread; it never locks, allocates or frees.
    class AudioEngine
    {
    public:
        static constexpr int maxTracks = 16;
        static constexpr int maxChunk = 4096;   // longer blocks are processed in chunks
        static constexpr int maxEventsPerChunk = 1024;

        AudioEngine();
        ~AudioEngine();

        void prepare (double sampleRate);
        void reset() noexcept;

        // --- Transport ----------------------------------------------------------
        void play() noexcept  { playRequested.store (true, std::memory_order_release); }
        void stop() noexcept  { playRequested.store (false, std::memory_order_release); }
        // Moves the playhead (also where Stop returns to).
        void seek (double tick) noexcept;
        double getPlayStart() const noexcept { return playStart.load (std::memory_order_acquire); }

        void setTempo (double bpm) noexcept;
        void setTimeSignature (time::TimeSignature) noexcept;
        void setMetronomeEnabled (bool) noexcept;
        void setLoop (bool enabled, time::Tick start, time::Tick end) noexcept;

        TransportState getState() const noexcept           { return state.load (std::memory_order_acquire); }
        bool isPlayRequested() const noexcept              { return playRequested.load (std::memory_order_acquire); }
        double getTempo() const noexcept                   { return tempo.load (std::memory_order_acquire); }
        time::TimeSignature getTimeSignature() const noexcept;
        bool isMetronomeEnabled() const noexcept           { return metronomeEnabled.load (std::memory_order_acquire); }
        bool isLoopEnabled() const noexcept                { return loopEnabled.load (std::memory_order_acquire); }
        time::Tick getLoopStart() const noexcept           { return loopStart.load (std::memory_order_acquire); }
        time::Tick getLoopEnd() const noexcept             { return loopEnd.load (std::memory_order_acquire); }

        // Musical position (ticks) and samples rendered since Play.
        double getPositionTicks() const noexcept           { return positionTicks.load (std::memory_order_acquire); }
        std::int64_t getPositionSamples() const noexcept   { return positionSamples.load (std::memory_order_acquire); }
        double getSampleRate() const noexcept              { return sampleRate.load (std::memory_order_acquire); }
        std::uint64_t getBeatCount() const noexcept        { return beatCount.load (std::memory_order_relaxed); }

        // --- Tracks (message thread) ----------------------------------------------
        void setSlotType (int slot, SlotType) noexcept;
        SlotType getSlotType (int slot) const noexcept;
        void setSlotMix (int slot, float volumeDb, float pan, bool muted, bool soloed) noexcept;
        // Replaces what the slot plays (nullptr = nothing).
        void setSlotSchedule (int slot, std::unique_ptr<TrackSchedule>);
        // Frees schedules the audio thread has finished with. Call regularly.
        void collectGarbage();

        DrumMachine& drums (int slot) noexcept;
        InstrumentRack& instruments (int slot) noexcept;
        int getActiveVoices (int slot) const noexcept;

        // Editor auditions (not on the audio thread): note on until note off.
        void previewNoteOn (int slot, int pitch, int velocity) noexcept  { previews.push ({ slot, pitch, velocity }); }
        void previewNoteOff (int slot, int pitch) noexcept               { previews.push ({ slot, pitch, 0 }); }

        // Writes the output for one block into every output channel.
        void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

        LevelMeter& getOutputMeter() noexcept { return outputMeter; }

    private:
        struct Slot;
        void processChunk (float* const* pair, int numPair, int numSamples) noexcept;
        void applySettings() noexcept;
        void publishPosition() noexcept;
        void renderVoices (int start, int num) noexcept;
        void releaseNotesNotCovering (Slot&, double tick) noexcept;
        void mixSlots (float* const* pair, int numPair, int numSamples) noexcept;
        static int clampSlot (int s) noexcept { return s < 0 ? 0 : (s >= maxTracks ? maxTracks - 1 : s); }

        // Audio-thread state.
        time::Transport transport;
        Metronome metronome;
        LevelMeter outputMeter;
        std::array<std::unique_ptr<Slot>, maxTracks> slots;
        std::vector<float> metronomeBuffer;
        PreviewQueue previews;
        bool wasPlaying = false;

        // Settings written by other threads.
        std::atomic<bool> playRequested { false };
        std::atomic<double> tempo { time::defaultTempo };
        std::atomic<std::uint32_t> packedSignature;
        std::atomic<bool> metronomeEnabled { false };
        std::atomic<bool> loopEnabled { false };
        std::atomic<time::Tick> loopStart { 0 }, loopEnd { 0 };
        std::atomic<double> playStart { 0.0 };
        std::atomic<bool> seekPending { false };

        // Published by the audio thread.
        std::atomic<TransportState> state { TransportState::stopped };
        std::atomic<double> positionTicks { 0.0 };
        std::atomic<std::int64_t> positionSamples { 0 };
        std::atomic<double> sampleRate { 0.0 };
        std::atomic<std::uint64_t> beatCount { 0 };
    };
}
