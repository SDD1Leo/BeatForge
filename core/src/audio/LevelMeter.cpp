#include "beatforge/audio/LevelMeter.h"

#include <algorithm>
#include <cmath>

namespace beatforge::audio
{
    static_assert (std::atomic<float>::is_always_lock_free);

    void LevelMeter::process (const float* const* channels, int numChannels, int numSamples) noexcept
    {
        for (int ch = 0; ch < std::min (numChannels, maxChannels); ++ch)
        {
            float blockPeak = 0.0f;

            for (int i = 0; i < numSamples; ++i)
                blockPeak = std::max (blockPeak, std::abs (channels[ch][i]));

            auto& stored = peaks[(size_t) ch];
            auto current = stored.load (std::memory_order_relaxed);

            while (blockPeak > current
                   && ! stored.compare_exchange_weak (current, blockPeak, std::memory_order_relaxed))
            {
            }
        }
    }

    float LevelMeter::takePeak (int channel) noexcept
    {
        if (channel < 0 || channel >= maxChannels)
            return 0.0f;

        return peaks[(size_t) channel].exchange (0.0f, std::memory_order_relaxed);
    }

    float LevelMeter::toDecibels (float linear, float floorDb) noexcept
    {
        return linear > 0.0f ? std::max (floorDb, 20.0f * std::log10 (linear)) : floorDb;
    }
}
