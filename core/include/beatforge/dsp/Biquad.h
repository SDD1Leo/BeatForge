#pragma once

namespace beatforge::dsp
{
    // Second-order IIR filter (RBJ cookbook coefficients, transposed direct
    // form II). Real-time safe.
    class Biquad
    {
    public:
        enum class Type { lowPass, highPass, bandPass };

        void setup (Type, double sampleRate, double frequency, double q) noexcept;
        void reset() noexcept { z1 = z2 = 0.0; }

        float process (float x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }

    private:
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
    };
}
