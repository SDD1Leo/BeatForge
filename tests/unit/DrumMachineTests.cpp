#include "EngineHelpers.h"

#include <beatforge/audio/AudioEngine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace beatforge::audio;

namespace
{
    constexpr int kick = (int) DrumSound::kick, snare = (int) DrumSound::snare,
                  closedHat = (int) DrumSound::closedHat, openHat = (int) DrumSound::openHat,
                  clap = (int) DrumSound::clap;

    // Renders the engine in fixed blocks and returns the left channel.
    std::vector<float> render (AudioEngine& engine, int numSamples, int blockSize)
    {
        std::vector<float> left ((size_t) blockSize), right ((size_t) blockSize), out;
        float* chans[] { left.data(), right.data() };
        out.reserve ((size_t) numSamples);

        for (int done = 0; done < numSamples; done += blockSize)
        {
            const int n = std::min (blockSize, numSamples - done);
            engine.process (chans, 2, n);
            out.insert (out.end(), left.begin(), left.begin() + n);
        }
        return out;
    }

    std::vector<size_t> onsets (const std::vector<float>& x, size_t minSilence = 100)
    {
        std::vector<size_t> result;
        size_t silent = minSilence;
        for (size_t i = 0; i < x.size(); ++i)
        {
            if (x[i] != 0.0f && silent >= minSilence)
                result.push_back (i);
            silent = x[i] == 0.0f ? silent + 1 : 0;
        }
        return result;
    }

    float peakIn (const std::vector<float>& x, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }

    // One drum track (slot 0) with a 16-bar clip whose pattern the test edits.
    struct Fixture
    {
        Fixture (double rate = 48000.0)
        {
            engine.prepare (rate);
            beatforge::test::playPattern (engine, 0, pat, 16);
        }
        DrumMachine& drums() { return engine.drums (0); }
        void step (int lane, int s)
        {
            pat[(size_t) lane][(size_t) s] = true;
            beatforge::test::playPattern (engine, 0, pat, 16);
        }
        AudioEngine engine;
        beatforge::project::DrumPattern pat {};
    };
}

TEST_CASE ("Kicks land on their exact step samples", "[drums]")
{
    // Steps 0 and 8: 1 s apart, longer than the 0.55 s kick, so each onset
    // follows silence and can be located exactly.
    Fixture f;
    f.step (kick, 0);
    f.step (kick, 8);

    f.engine.play();
    REQUIRE (onsets (render (f.engine, 96000 * 2, 512)) == std::vector<size_t> { 0, 48000, 96000, 144000 });
}

TEST_CASE ("Four-on-the-floor triggers every beat", "[drums]")
{
    Fixture f;
    for (int step : { 0, 4, 8, 12 })
        f.step (kick, step);

    f.engine.play();
    render (f.engine, 96000 * 2, 512);   // 2 bars @ 120 BPM
    REQUIRE (f.drums().getTriggerCount (kick) == 8);
}

TEST_CASE ("16th-note hats hit every step", "[drums]")
{
    Fixture f;
    for (int step = 0; step < 16; ++step)
        f.step (closedHat, step);

    f.engine.play();
    const auto hits = onsets (render (f.engine, 96000, 300));
    REQUIRE (hits.size() == 16);
    for (size_t i = 0; i < hits.size(); ++i)
        REQUIRE (hits[i] == i * 6000 + 1);   // the hat's attack ramp starts at exactly 0.0
}

TEST_CASE ("Pattern loops every bar and follows the tempo", "[drums]")
{
    Fixture f;
    f.engine.setTempo (100.0);   // 16th = 0.15 s = 7200 samples
    f.step (clap, 4);
    f.step (clap, 12);

    f.engine.play();
    const auto hits = onsets (render (f.engine, 7200 * 16 * 3, 480));
    REQUIRE (hits.size() == 6);
    for (size_t i = 0; i < hits.size(); ++i)
        REQUIRE (hits[i] == 7200 * (4 + 8 * i));
}

TEST_CASE ("Drum output is identical for any block size", "[drums]")
{
    const int blockSize = GENERATE (1, 37, 64, 256, 1000);
    CAPTURE (blockSize);

    auto makeBeat = [] (Fixture& f)
    {
        for (int s : { 0, 4, 8, 12 }) f.step (kick, s);
        for (int s : { 4, 12 })       f.step (snare, s);
        for (int s = 0; s < 16; s += 2) f.step (closedHat, s);
        f.step (openHat, 14);
        f.step (clap, 12);
        f.engine.setTempo (127.0);
        f.engine.play();
    };

    Fixture a, b;
    makeBeat (a);
    makeBeat (b);
    const auto reference = render (a.engine, 48000 * 6, 4096);
    const auto chunked = render (b.engine, 48000 * 6, blockSize);

    REQUIRE (reference.size() == chunked.size());
    REQUIRE (reference == chunked);   // bit-exact: no block-boundary double or missed hits
}

TEST_CASE ("Step edits during playback apply from the next step", "[drums]")
{
    Fixture f;
    f.step (kick, 0);
    f.engine.play();

    auto first = render (f.engine, 30000, 512);    // past step 4 (24000)
    f.step (kick, 8);              // step 8 is at 48000
    auto rest = render (f.engine, 66000, 512);
    first.insert (first.end(), rest.begin(), rest.end());

    const auto hits = onsets (first);
    REQUIRE (hits == std::vector<size_t> { 0, 48000 });
}

TEST_CASE ("Mute, solo and lane volume", "[drums][controls]")
{
    auto snareAndKick = [] (Fixture& f)
    {
        render (f.engine, 1024, 512);   // device running while the user clicks: ramps settle
        f.step (kick, 0);
        f.step (snare, 4);
        f.engine.play();
        return render (f.engine, 48000, 512);
    };

    Fixture normal;
    const auto full = snareAndKick (normal);
    const float kickPeak = peakIn (full, 0, 2000);
    REQUIRE (kickPeak > 0.2f);

    // The kick (0.55 s) still rings under the snare, so measure the snare alone.
    const float snarePeak = [&]
    {
        const auto kit = DrumKit::generate (48000.0);   // keep the kit alive while iterating its sample
        float p = 0.0f;
        for (auto v : kit.sample (DrumSound::snare)) p = std::max (p, std::abs (v));
        return p;
    }();

    SECTION ("muted lane is silent")
    {
        Fixture f;
        f.drums().setLaneMuted (kick, true);
        const auto out = snareAndKick (f);
        REQUIRE (peakIn (out, 0, 20000) == 0.0f);
        REQUIRE (peakIn (out, 24000, 26000) == Catch::Approx (snarePeak));
    }

    SECTION ("solo isolates a lane")
    {
        Fixture f;
        f.drums().setLaneSoloed (snare, true);
        const auto out = snareAndKick (f);
        REQUIRE (peakIn (out, 0, 20000) == 0.0f);
        REQUIRE (peakIn (out, 24000, 26000) == Catch::Approx (snarePeak));
    }

    SECTION ("-6 dB halves the level")
    {
        Fixture f;
        f.drums().setLaneGainDb (kick, -6.0206f);
        const auto out = snareAndKick (f);
        REQUIRE (peakIn (out, 0, 2000) == Catch::Approx (kickPeak * 0.5f).epsilon (0.001));
    }

    SECTION ("minimum volume is silence")
    {
        Fixture f;
        f.drums().setLaneGainDb (kick, -100.0f);
        REQUIRE (f.drums().getLaneGainDb (kick) == DrumMachine::minGainDb);
        REQUIRE (peakIn (snareAndKick (f), 0, 20000) == 0.0f);
    }
}

TEST_CASE ("Preview plays a lane while stopped", "[drums]")
{
    Fixture f;
    f.drums().preview (snare);
    const auto out = render (f.engine, 9600, 512);
    REQUIRE (onsets (out) == std::vector<size_t> { 0 });
    REQUIRE (peakIn (out, 0, 9600) > 0.1f);
    REQUIRE (f.engine.getState() == TransportState::stopped);
    REQUIRE (f.drums().getTriggerCount (snare) == 1);
}

TEST_CASE ("Closed hat chokes the open hat", "[drums]")
{
    auto openHatTail = [] (bool withClosedHat)
    {
        Fixture f;
        f.step (openHat, 0);
        if (withClosedHat)
            f.step (closedHat, 2);   // 12000 samples; closed hat ends ~17760
        f.engine.play();
        const auto out = render (f.engine, 24000, 512);
        return peakIn (out, 19000, 23000);
    };

    REQUIRE (openHatTail (false) > 0.003f);  // open hat still ringing
    REQUIRE (openHatTail (true) == 0.0f);    // choked
}

TEST_CASE ("Stopping lets drum tails ring but triggers nothing new", "[drums]")
{
    Fixture f;
    for (int s = 0; s < 16; ++s) f.step (kick, s);
    f.engine.play();
    render (f.engine, 1000, 500);
    f.engine.stop();

    const auto tail = render (f.engine, 48000, 500);
    REQUIRE (peakIn (tail, 0, 500) > 0.1f);                  // ringing
    REQUIRE (onsets (tail).size() == 1);                     // no further hits
    REQUIRE (peakIn (tail, 30000, 48000) == 0.0f);           // and then silence
}

TEST_CASE ("Muting while a drum rings fades it out within 5 ms", "[drums][controls]")
{
    const int blockSize = GENERATE (64, 4096);
    CAPTURE (blockSize);

    Fixture f;
    f.step (kick, 0);
    f.engine.play();
    render (f.engine, 4096, blockSize);           // kick ringing
    f.drums().setLaneMuted (kick, true);
    const auto after = render (f.engine, 8192, blockSize);

    REQUIRE (peakIn (after, 0, 64) > 0.05f);      // no hard cut
    REQUIRE (peakIn (after, 241, 8192) == 0.0f);  // silent after the 240-sample ramp
}
