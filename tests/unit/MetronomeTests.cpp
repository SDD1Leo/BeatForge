#include <beatforge/audio/Metronome.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using beatforge::audio::Metronome;

namespace
{
    std::vector<float> renderClick (bool accent, int numSamples = 4800)
    {
        Metronome m;
        m.prepare (48000.0);
        m.trigger (accent);
        std::vector<float> out ((size_t) numSamples, 0.0f);
        float* ch[] { out.data() };
        m.render (ch, 1, 0, numSamples);
        return out;
    }

    float peak (const std::vector<float>& v)
    {
        float p = 0.0f;
        for (auto s : v) p = std::max (p, std::abs (s));
        return p;
    }
}

TEST_CASE ("Clicks are short, decaying and end in silence", "[audio][metronome]")
{
    const auto click = renderClick (false);
    const auto length = (size_t) (Metronome::clickSeconds * 48000.0);

    REQUIRE (peak (click) > 0.2f);
    REQUIRE (peak (click) <= Metronome::normalGain);
    REQUIRE (std::all_of (click.begin() + (long) length, click.end(), [] (float s) { return s == 0.0f; }));

    // Last few samples before the cut are already tiny (no click at the end).
    for (size_t i = length - 10; i < length; ++i)
        REQUIRE (std::abs (click[i]) < 0.002f);
}

TEST_CASE ("Downbeat click is louder and higher", "[audio][metronome]")
{
    const auto accent = renderClick (true), normal = renderClick (false);
    REQUIRE (peak (accent) > peak (normal) * 1.4f);

    // Rising zero crossings over the first 20 ms (the start at sin(0) is not one).
    auto crossings = [] (const std::vector<float>& v)
    {
        int n = 0;
        for (size_t i = 1; i < 960; ++i)
            if (v[i - 1] < 0.0f && v[i] >= 0.0f) ++n;
        return n;
    };
    REQUIRE (std::abs (crossings (accent) - 30) <= 1);   // 1500 Hz * 20 ms
    REQUIRE (std::abs (crossings (normal) - 20) <= 1);   // 1000 Hz * 20 ms
}

TEST_CASE ("Click renders identically in pieces", "[audio][metronome]")
{
    const auto whole = renderClick (true);

    Metronome m;
    m.prepare (48000.0);
    m.trigger (true);
    std::vector<float> pieces (whole.size(), 0.0f);
    float* ch[] { pieces.data() };
    for (int start = 0; start < (int) pieces.size(); start += 37)
        m.render (ch, 1, start, std::min (37, (int) pieces.size() - start));

    REQUIRE (pieces == whole);
}
