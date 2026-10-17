#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beatforge::dsp
{
    // Topology-preserving-transform state-variable low-pass (Zavalishin). Stays
    // stable when the cutoff moves every sample and at high resonance, which
    // a direct-form biquad does not. resonance 0..1 maps to Q 0.5..~12.
    class StateVariableLowPass
    {
    public:
        void setSampleRate (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 48000.0; }
        void reset() noexcept                   { ic1 = ic2 = 0.0; }

        void set (double cutoffHz, double resonance) noexcept
        {
            cutoffHz = std::clamp (cutoffHz, 20.0, sampleRate * 0.45);
            const double q = 0.5 + std::clamp (resonance, 0.0, 1.0) * 11.5;
            g = std::tan (std::numbers::pi * cutoffHz / sampleRate);
            k = 1.0 / q;
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        float process (float x) noexcept
        {
            const double v3 = x - ic2;
            const double v1 = a1 * ic1 + a2 * v3;
            const double v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return (float) v2;
        }

        // Magnitude response of the current settings (for the UI curve).
        static double magnitude (double frequency, double cutoffHz, double resonance, double sampleRate) noexcept
        {
            const double q = 0.5 + std::clamp (resonance, 0.0, 1.0) * 11.5;
            const double w = std::tan (std::numbers::pi * std::min (frequency, sampleRate * 0.49) / sampleRate)
                           / std::tan (std::numbers::pi * std::clamp (cutoffHz, 20.0, sampleRate * 0.45) / sampleRate);
            const double re = 1.0 - w * w, im = w / q;
            return 1.0 / std::sqrt (re * re + im * im);
        }

    private:
        double sampleRate = 48000.0;
        double g = 0.0, k = 2.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double ic1 = 0.0, ic2 = 0.0;
    };
}
