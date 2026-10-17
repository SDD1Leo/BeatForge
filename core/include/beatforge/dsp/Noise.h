#pragma once

#include <cstdint>

namespace beatforge::dsp
{
    // Deterministic white noise in [-1, 1) (xorshift32). The same seed always
    // gives the same sequence, so generated sounds and renders are repeatable.
    class Noise
    {
    public:
        explicit Noise (std::uint32_t seed = 0x9e3779b9u) noexcept : state (seed != 0 ? seed : 1u) {}

        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) ((double) state / 2147483648.0 - 1.0);
        }

    private:
        std::uint32_t state;
    };
}
