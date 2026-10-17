#pragma once

#include <array>
#include <atomic>

namespace beatforge::audio
{
    // Peak meter shared between the audio thread (writer) and the UI (reader).
    // The audio thread folds block peaks in with a lock-free max; the UI takes
    // and resets the accumulated peak at its own frame rate.
    class LevelMeter
    {
    public:
        static constexpr int maxChannels = 2;

        // Audio thread.
        void process (const float* const* channels, int numChannels, int numSamples) noexcept;

        // UI thread: linear peak since the previous call, then reset to zero.
        float takePeak (int channel) noexcept;

        static float toDecibels (float linear, float floorDb = -100.0f) noexcept;

    private:
        std::array<std::atomic<float>, maxChannels> peaks {};
    };
}
