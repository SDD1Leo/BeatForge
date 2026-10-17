#pragma once

#include <cmath>
#include <numbers>

namespace beatforge::dsp
{
    enum class Waveform { sine, saw, square, triangle };

    constexpr int numWaveforms = 4;

    // Phase-accumulator oscillator. Saw and square use PolyBLEP to reduce
    // aliasing; triangle is a leaky integration of the band-limited square.
    // Output is roughly within [-1, 1]. Real-time safe.
    class Oscillator
    {
    public:
        void setSampleRate (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 48000.0; }
        void setFrequency (double hz) noexcept  { increment = hz / sampleRate; }
        void resetPhase() noexcept              { phase = 0.0; triangleState = 0.0; }

        float next (Waveform w) noexcept
        {
            double out = 0.0;
            const double dt = increment;

            switch (w)
            {
                case Waveform::sine:
                    out = std::sin (2.0 * std::numbers::pi * phase);
                    break;

                case Waveform::saw:
                    out = 2.0 * phase - 1.0 - polyBlep (phase, dt);
                    break;

                case Waveform::square:
                case Waveform::triangle:
                {
                    double sq = phase < 0.5 ? 1.0 : -1.0;
                    sq += polyBlep (phase, dt);
                    double shifted = phase + 0.5;
                    shifted -= std::floor (shifted);
                    sq -= polyBlep (shifted, dt);

                    if (w == Waveform::square)
                    {
                        out = sq;
                    }
                    else
                    {
                        // Integrate the square: slope 4*dt per sample gives a +-1 triangle.
                        triangleState = 0.999 * triangleState + 4.0 * dt * sq;
                        out = triangleState;
                    }
                    break;
                }
            }

            phase += dt;
            phase -= std::floor (phase);
            return (float) out;
        }

    private:
        static double polyBlep (double t, double dt) noexcept
        {
            if (dt <= 0.0) return 0.0;
            if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
            return 0.0;
        }

        double sampleRate = 48000.0, phase = 0.0, increment = 0.0, triangleState = 0.0;
    };
}
