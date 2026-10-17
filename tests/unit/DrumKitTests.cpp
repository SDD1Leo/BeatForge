#include <beatforge/audio/DrumKit.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>

using namespace beatforge::audio;

namespace
{
    float peak (const std::vector<float>& s)
    {
        float p = 0.0f;
        for (auto v : s) p = std::max (p, std::abs (v));
        return p;
    }

    // Zero crossings per second over the loud part of the sound.
    double crossingRate (const std::vector<float>& s, double rate)
    {
        const auto n = std::min (s.size(), (size_t) (0.05 * rate));
        int crossings = 0;
        for (size_t i = 1; i < n; ++i)
            if ((s[i - 1] < 0.0f) != (s[i] < 0.0f)) ++crossings;
        return crossings / ((double) n / rate);
    }
}

TEST_CASE ("Every built-in drum is a usable one-shot", "[audio][drumkit]")
{
    const double rate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (rate);
    const auto kit = DrumKit::generate (rate);

    for (int i = 0; i < numDrumSounds; ++i)
    {
        const auto& s = kit.samples[(size_t) i];
        CAPTURE (drumSoundName ((DrumSound) i));

        REQUIRE (s.size() > (size_t) (0.1 * rate));
        REQUIRE (s.size() < (size_t) (0.7 * rate));
        REQUIRE (std::all_of (s.begin(), s.end(), [] (float v) { return std::isfinite (v); }));
        REQUIRE (peak (s) > 0.15f);
        REQUIRE (peak (s) <= 0.45f);          // headroom for several drums at once
        REQUIRE (s.back() == 0.0f);           // ends exactly at zero (no click)

        // Attack: loud within the first 2 ms, so hits sound on the step.
        std::vector<float> head (s.begin(), s.begin() + (long) (0.002 * rate));
        REQUIRE (peak (head) > 0.1f * peak (s));
    }
}

TEST_CASE ("Kit generation is deterministic", "[audio][drumkit]")
{
    const auto a = DrumKit::generate (48000.0), b = DrumKit::generate (48000.0);
    REQUIRE (a.samples == b.samples);
}

TEST_CASE ("Drums have the expected character", "[audio][drumkit]")
{
    const auto kit = DrumKit::generate (48000.0);
    const auto kick = crossingRate (kit.sample (DrumSound::kick), 48000.0);
    const auto snare = crossingRate (kit.sample (DrumSound::snare), 48000.0);
    const auto hat = crossingRate (kit.sample (DrumSound::closedHat), 48000.0);
    CAPTURE (kick, snare, hat);

    REQUIRE (kick < 400.0);                // low thump
    REQUIRE (snare > kick * 4.0);
    REQUIRE (hat > 8000.0);                // bright
    REQUIRE (kit.sample (DrumSound::openHat).size() > kit.sample (DrumSound::closedHat).size() * 3);
}

TEST_CASE ("A full beat at default volumes does not clip", "[audio][drumkit]")
{
    // Worst case of the built-in kit: every drum on the same sample.
    const auto kit = DrumKit::generate (48000.0);
    size_t longest = 0;
    for (auto& s : kit.samples) longest = std::max (longest, s.size());

    std::vector<float> sum (longest, 0.0f);
    for (auto& s : kit.samples)   // all five drums on one step (open + closed hat included)
        for (size_t i = 0; i < s.size(); ++i)
            sum[i] += s[i];

    CAPTURE (peak (sum));
    REQUIRE (peak (sum) < 0.95f);
}
