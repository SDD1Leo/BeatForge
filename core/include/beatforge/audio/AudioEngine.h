#pragma once

#include "beatforge/audio/DrumMachine.h"
#include "beatforge/audio/Instruments.h"
#include "beatforge/audio/LevelMeter.h"
#include "beatforge/audio/Metronome.h"
#include "beatforge/audio/PreviewQueue.h"
#include "beatforge/audio/TrackSchedule.h"
#include "beatforge/time/Transport.h"
#include "beatforge/util/LockFreeExchange.h"
#include "beatforge/util/SpscQueue.h"

#include <algorithm>
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

        // --- Live input (MIDI keyboard, on-screen keyboard) ---------------------------------
        // Notes play on the live target slot (an instrument slot; -1 = none) and,
        // while recording and playing, are reported back with their musical position.
        //
        // Events are stamped with now() when they arrive. The engine follows the
        // audio clock (callbacks may come in bursts, e.g. through a sound server)
        // and plays every event a fixed delay after it arrived - one block plus
        // the observed burst - so the spacing between notes is kept exactly.
        // pitch -1 with velocity 0 = all notes off.
        struct LiveNote
        {
            double time = 0.0;   // now() when the note arrived
            int pitch = 60;
            int velocity = 0;    // 0 = note off
        };

        static double now() noexcept;                         // steady clock, seconds
        bool deviceNote (const LiveNote& n) noexcept   { return deviceInput.push (n); }   // MIDI thread
        bool screenNote (const LiveNote& n) noexcept   { return screenInput.push (n); }   // message thread
        void setLiveTarget (int slot) noexcept         { liveTarget.store (slot, std::memory_order_release); }
        int getLiveTarget() const noexcept             { return liveTarget.load (std::memory_order_acquire); }

        // Recording: what the live input plays while the transport runs is reported
        // through popRecorded(), at the musical position the player heard when
        // pressing the key: the press time on the audio clock, moved back by
        // `outputLatency` (samples from rendering to the speakers).
        void setRecording (bool) noexcept;
        bool isRecording() const noexcept              { return recording.load (std::memory_order_acquire); }
        void setOutputLatency (int samples) noexcept   { outputLatency.store (std::max (0, samples), std::memory_order_release); }

        struct RecordedEvent
        {
            double tick = 0.0;
            int pitch = 60;      // endOfTake: recording stopped at `tick`, close held notes
            int velocity = 0;    // 0 = note off
            static constexpr int endOfTake = -1;
        };
        bool popRecorded (RecordedEvent& e) noexcept   { return recorded.pop (e); }

        // Tests: replace the clock used to place live events.
        using Clock = double (*)() noexcept;
        void setClockForTesting (Clock c) noexcept     { clock = c != nullptr ? c : &AudioEngine::now; }

        static constexpr VoiceId liveVoiceBase = VoiceId (1) << 62;
        static constexpr bool isLiveVoice (VoiceId v) noexcept { return v != PolySynthInstrument::previewId && (v & liveVoiceBase) != 0; }

        // Writes the output for one block into every output channel.
        void process (float* const* outputs, int numOutputs, int numSamples) noexcept;

        LevelMeter& getOutputMeter() noexcept { return outputMeter; }

    private:
        struct Slot;
        void processChunk (float* const* pair, int numPair, int numSamples, int blockOffset) noexcept;
        void collectLiveEvents (int numSamples) noexcept;
        bool applyLive (const LiveNote&) noexcept;
        void releaseLive (double tick, double samplesPerTick) noexcept;
        // `tick` = where it was rendered; `delaySamples` = from what the player heard to that.
        void recordEvent (double tick, double delaySamples, double samplesPerTick, int pitch, int velocity) noexcept;
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

        // Live input (audio thread unless noted).
        util::SpscQueue<LiveNote, 512> deviceInput, screenInput;     // producers: MIDI thread / message thread
        util::SpscQueue<RecordedEvent, 4096> recorded;               // consumer: message thread
        struct PlacedLive
        {
            int offset;           // where it is rendered in this block
            double pressOffset;   // where the key press falls on the audio clock (may be < 0)
            LiveNote note;
        };
        static constexpr int maxLivePerBlock = 256;
        std::array<PlacedLive, maxLivePerBlock> liveEvents {};
        int numLiveEvents = 0;
        std::array<bool, 128> liveHeld {};
        int liveSlot = -1;                                            // slot the held notes play on
        bool recordingActive = false;                                 // recording && playing
        double lastPlayingTick = 0.0, lastSamplesPerTick = 1.0;
        Clock clock = &AudioEngine::now;
        std::atomic<int> liveTarget { -1 };
        std::atomic<bool> recording { false };
        std::atomic<int> outputLatency { 0 };

        // Audio clock (audio thread): wall time of the latest callback deadline,
        // as `clockOffset + renderedSamples / sampleRate`.
        std::int64_t renderedSamples = 0;
        double clockOffset = 0.0, clockEarliness = 0.0;
        bool clockValid = false;

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
