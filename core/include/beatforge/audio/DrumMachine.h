#pragma once

#include "beatforge/audio/DrumKit.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace beatforge::audio
{
    // 5-lane sample drum machine: plays the hits it is given (patterns live in
    // clips and arrive as scheduled hits).
    //
    // Lane controls are atomics: the UI writes them, the audio thread reads
    // them each block. Voices and the kit are audio-thread state (the kit is
    // rebuilt in prepare(), which must not overlap render()).
    class DrumMachine
    {
    public:
        static constexpr int numLanes = numDrumSounds;
        static constexpr int numSteps = 16;

        DrumMachine();

        // Builds the kit for the sample rate (allocates).
        void prepare (double sampleRate);
        void reset() noexcept;

        // --- Lane controls: any thread -----------------------------------------
        void setLaneGainDb (int lane, float db) noexcept;   // <= -60 dB is silence
        float getLaneGainDb (int lane) const noexcept;
        void setLaneMuted (int lane, bool) noexcept;
        bool isLaneMuted (int lane) const noexcept;
        void setLaneSoloed (int lane, bool) noexcept;
        bool isLaneSoloed (int lane) const noexcept;

        // Plays a lane's sound at the start of the next block (step audition).
        void preview (int lane) noexcept;

        // Increments each time the lane's sound starts (UI trigger lights).
        std::uint32_t getTriggerCount (int lane) const noexcept;

        // --- Audio thread ----------------------------------------------------
        // Starts every lane whose bit is set in `laneMask`.
        void triggerLanes (std::uint8_t laneMask) noexcept;
        void trigger (int lane) noexcept;
        void startPendingPreviews() noexcept;

        // Called once per block before rendering: picks up lane gain changes,
        // which then ramp over gainRampSeconds (independent of block size).
        void beginBlock() noexcept;

        static constexpr double gainRampSeconds = 0.005;

        // Adds the drums into channels[ch][start .. start + num).
        void render (float* const* channels, int numChannels, int start, int num) noexcept;

        bool isSounding() const noexcept;

        static constexpr float minGainDb = -60.0f, maxGainDb = 6.0f;

    private:
        struct Voice
        {
            const float* data = nullptr;
            int length = 0, position = 0;
            int fadeRemaining = 0;   // > 0: fading out, fadeRemaining of fadeLength samples left
            int fadeLength = 1;
            bool active = false;
        };

        // Two voices per lane so a retrigger can fade the old hit while the new
        // one starts: no clicks on fast repeats.
        struct Lane
        {
            std::array<Voice, 2> voices;
            int current = 0;
            float gain = 1.0f, gainStep = 0.0f, targetGain = 1.0f;
        };

        void fadeOut (Voice&, int samples) noexcept;
        float laneTargetGain (int lane, bool anySolo) const noexcept;
        static float rampTowards (float gain, float target, float step, int samples) noexcept;

        DrumKit kit;
        std::array<Lane, numLanes> lanes;
        int gainRampSamples = 240;
        int retriggerFade = 64, chokeFade = 240;

        std::array<std::atomic<float>, numLanes> gainDb;
        std::array<std::atomic<bool>, numLanes> muted, soloed, previewPending;
        std::array<std::atomic<std::uint32_t>, numLanes> triggerCounts;
    };
}
