#include <beatforge/audio/TestTone.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using beatforge::audio::TestTone;

namespace
{
    std::vector<float> renderMono (TestTone& tone, int numSamples, int blockSize)
    {
        std::vector<float> out ((size_t) numSamples);

        for (int start = 0; start < numSamples; start += blockSize)
        {
            float* ch[] { out.data() + start };
            tone.render (ch, 1, std::min (blockSize, numSamples - start));
        }

        return out;
    }

    int countRisingZeroCrossings (const std::vector<float>& s, size_t from)
    {
        int n = 0;
        for (size_t i = std::max<size_t> (from, 1); i < s.size(); ++i)
            if (s[i - 1] < 0.0f && s[i] >= 0.0f)
                ++n;
        return n;
    }

    float peak (const std::vector<float>& s, size_t from = 0)
    {
        float p = 0.0f;
        for (size_t i = from; i < s.size(); ++i)
            p = std::max (p, std::abs (s[i]));
        return p;
    }
}

TEST_CASE ("Test tone is silent until activated", "[audio][tone]")
{
    TestTone tone;
    tone.prepare (48000.0);
    REQUIRE_FALSE (tone.isSounding());
    REQUIRE (peak (renderMono (tone, 4800, 512)) == 0.0f);
}

TEST_CASE ("Test tone has the right frequency and level at common sample rates", "[audio][tone]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (sampleRate);

    TestTone tone;
    tone.prepare (sampleRate);
    tone.setActive (true);

    const auto oneSecond = renderMono (tone, (int) sampleRate, 480);
    const auto rampLength = (size_t) (TestTone::rampSeconds * sampleRate) + 1;

    // 440 cycles in one second (allow one for the start/edge).
    REQUIRE (std::abs (countRisingZeroCrossings (oneSecond, 0) - 440) <= 1);

    // Full level after the ramp is -18 dBFS.
    const float expected = std::pow (10.0f, TestTone::defaultGainDb / 20.0f);
    REQUIRE (peak (oneSecond, rampLength) == Catch::Approx (expected).margin (1e-3));
}

TEST_CASE ("Test tone starts and stops without clicks", "[audio][tone]")
{
    TestTone tone;
    tone.prepare (48000.0);
    tone.setActive (true);

    auto start = renderMono (tone, 4800, 256);
    REQUIRE (start.front() == 0.0f);

    // Largest per-sample jump of a -18 dBFS 440 Hz sine at 48k is ~0.0072;
    // a click would be an order of magnitude bigger.
    for (size_t i = 1; i < start.size(); ++i)
        REQUIRE (std::abs (start[i] - start[i - 1]) < 0.01f);

    tone.setActive (false);
    auto stop = renderMono (tone, 4800, 256);

    for (size_t i = 1; i < stop.size(); ++i)
        REQUIRE (std::abs (stop[i] - stop[i - 1]) < 0.01f);

    // Fully silent once the 10 ms ramp has elapsed.
    REQUIRE_FALSE (tone.isSounding());
    REQUIRE (peak (stop, 481) == 0.0f);
}

TEST_CASE ("Test tone output does not depend on block size", "[audio][tone]")
{
    const int blockSize = GENERATE (1, 31, 64, 512, 4096);
    CAPTURE (blockSize);

    TestTone reference, chunked;
    reference.prepare (44100.0);
    chunked.prepare (44100.0);
    reference.setActive (true);
    chunked.setActive (true);

    const auto a = renderMono (reference, 20000, 20000);
    const auto b = renderMono (chunked, 20000, blockSize);
    REQUIRE (a == b);
}

TEST_CASE ("Test tone writes the same signal to every channel", "[audio][tone]")
{
    TestTone tone;
    tone.prepare (48000.0);
    tone.setActive (true);

    std::vector<float> left (1024), right (1024, 99.0f);
    float* chans[] { left.data(), right.data() };
    tone.render (chans, 2, 1024);
    REQUIRE (left == right);
}
