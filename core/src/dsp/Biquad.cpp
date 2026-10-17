#include "beatforge/dsp/Biquad.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beatforge::dsp
{
    void Biquad::setup (Type type, double sampleRate, double frequency, double q) noexcept
    {
        frequency = std::clamp (frequency, 1.0, sampleRate * 0.49);
        const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
        const double cosW = std::cos (w);
        const double alpha = std::sin (w) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        double nb0 = 0, nb1 = 0, nb2 = 0;

        switch (type)
        {
            case Type::lowPass:  nb0 = (1.0 - cosW) / 2.0; nb1 = 1.0 - cosW;    nb2 = nb0; break;
            case Type::highPass: nb0 = (1.0 + cosW) / 2.0; nb1 = -(1.0 + cosW); nb2 = nb0; break;
            case Type::bandPass: nb0 = alpha;              nb1 = 0.0;           nb2 = -alpha; break;
        }

        b0 = nb0 / a0;
        b1 = nb1 / a0;
        b2 = nb2 / a0;
        a1 = -2.0 * cosW / a0;
        a2 = (1.0 - alpha) / a0;
    }
}
