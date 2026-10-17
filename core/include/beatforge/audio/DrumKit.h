#pragma once

#include <array>
#include <string_view>
#include <vector>

namespace beatforge::audio
{
    enum class DrumSound { kick, snare, closedHat, openHat, clap };

    constexpr int numDrumSounds = 5;

    std::string_view drumSoundName (DrumSound) noexcept;

    // One-shot samples for the built-in kit. generate() synthesises the default
    // sounds deterministically for a sample rate; later phases can replace a
    // sample with a user-loaded file. Allocates: never call on the audio thread.
    struct DrumKit
    {
        std::array<std::vector<float>, numDrumSounds> samples;

        const std::vector<float>& sample (DrumSound s) const noexcept { return samples[(size_t) s]; }

        static DrumKit generate (double sampleRate);
    };
}
