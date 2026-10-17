#include <beatforge/time/Transport.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using namespace beatforge::time;

namespace
{
    struct Fired
    {
        std::int64_t sample;
        Tick tick;
        int beatInBar;
    };

    // Runs the transport in fixed blocks and records absolute beat samples.
    struct Runner
    {
        Runner (double rate, double bpm, TimeSignature sig = {})
        {
            transport.setTempo (bpm);
            transport.setTimeSignature (sig);
            transport.prepare (rate);
        }

        void run (std::int64_t numSamples, int blockSize)
        {
            for (std::int64_t done = 0; done < numSamples;)
            {
                const int n = (int) std::min<std::int64_t> (blockSize, numSamples - done);
                const auto start = elapsed;
                transport.advance (n, [&] (const Transport::Beat& b)
                {
                    REQUIRE (b.sampleOffset >= 0);
                    REQUIRE (b.sampleOffset < n);
                    beats.push_back ({ start + b.sampleOffset, b.tick, b.beatInBar });
                });
                elapsed += n;
                done += n;
            }
        }

        Transport transport;
        std::vector<Fired> beats;
        std::int64_t elapsed = 0;
    };
}

TEST_CASE ("Beats land on exact samples at simple tempos", "[transport]")
{
    SECTION ("120 BPM @ 48 kHz: every 24000 samples")
    {
        Runner r (48000.0, 120.0);
        r.run (48000 * 4, 512);
        REQUIRE (r.beats.size() == 8);
        for (size_t i = 0; i < r.beats.size(); ++i)
        {
            REQUIRE (r.beats[i].sample == (std::int64_t) i * 24000);
            REQUIRE (r.beats[i].tick == (Tick) i * 960);
            REQUIRE (r.beats[i].beatInBar == (int) (i % 4) + 1);
        }
    }

    SECTION ("100 BPM @ 44.1 kHz: every 26460 samples")
    {
        Runner r (44100.0, 100.0);
        r.run (26460 * 10, 300);
        REQUIRE (r.beats.size() == 10);
        for (size_t i = 0; i < r.beats.size(); ++i)
            REQUIRE (r.beats[i].sample == (std::int64_t) i * 26460);
    }
}

TEST_CASE ("Beat timing does not depend on block size", "[transport]")
{
    const int blockSize = GENERATE (1, 7, 64, 441, 512, 4096);
    CAPTURE (blockSize);

    Runner reference (44100.0, 133.0), chunked (44100.0, 133.0);
    reference.run (44100 * 10, 44100 * 10);
    chunked.run (44100 * 10, blockSize);

    REQUIRE (reference.beats.size() == chunked.beats.size());
    for (size_t i = 0; i < reference.beats.size(); ++i)
    {
        REQUIRE (reference.beats[i].sample == chunked.beats[i].sample);
        REQUIRE (reference.beats[i].tick == chunked.beats[i].tick);
    }
}

TEST_CASE ("No drift over an hour of playback", "[transport]")
{
    const double bpm = GENERATE (120.0, 133.0, 97.3);
    const double rate = GENERATE (44100.0, 48000.0);
    CAPTURE (bpm, rate);

    Runner r (rate, bpm);
    r.run ((std::int64_t) (3600 * rate), 512);

    const double samplesPerBeat = 60.0 / bpm * rate;
    REQUIRE (r.beats.size() == (size_t) std::ceil (3600.0 / (60.0 / bpm)));

    for (size_t i = 0; i < r.beats.size(); ++i)
    {
        const double ideal = (double) i * samplesPerBeat;
        // Fires on the first sample at or after the ideal position.
        REQUIRE ((double) r.beats[i].sample >= ideal - 1e-6);
        REQUIRE ((double) r.beats[i].sample < ideal + 1.0);
    }
}

TEST_CASE ("Time signature sets beats per bar and accents", "[transport]")
{
    Runner r (48000.0, 120.0, { 6, 8 });   // eighth-note beats: 12000 samples apart
    r.run (12000 * 12, 256);
    REQUIRE (r.beats.size() == 12);

    for (size_t i = 0; i < r.beats.size(); ++i)
    {
        REQUIRE (r.beats[i].sample == (std::int64_t) i * 12000);
        REQUIRE (r.beats[i].beatInBar == (int) (i % 6) + 1);
    }
}

TEST_CASE ("Tempo change keeps beats on the musical grid", "[transport][tempo]")
{
    Runner r (48000.0, 120.0);
    r.run (30000, 500);                       // 1.25 beats at 120: beats at 0, 24000
    REQUIRE (r.beats.size() == 2);
    REQUIRE (r.transport.getPositionTicks() == Catch::Approx (1200.0));

    r.transport.setTempo (60.0);              // 50 samples per tick from here
    r.run (48000 * 4, 500);

    // Next beat is tick 1920: 720 ticks after the change at sample 30000.
    REQUIRE (r.beats[2].tick == 1920);
    REQUIRE (r.beats[2].sample == 30000 + 720 * 50);
    for (size_t i = 3; i < r.beats.size(); ++i)
    {
        REQUIRE (r.beats[i].tick == r.beats[i - 1].tick + 960);
        REQUIRE (r.beats[i].sample - r.beats[i - 1].sample == 48000);
    }
}

TEST_CASE ("Many tempo changes never duplicate or skip a beat", "[transport][tempo]")
{
    Runner r (44100.0, 120.0);
    const double tempos[] { 120.0, 121.0, 87.5, 240.0, 40.0, 133.3, 174.0 };

    for (int block = 0; block < 4000; ++block)
    {
        if (block % 13 == 0)
            r.transport.setTempo (tempos[(size_t) (block / 13) % std::size (tempos)]);
        r.run (256, 256);
    }

    REQUIRE_FALSE (r.beats.empty());
    for (size_t i = 0; i < r.beats.size(); ++i)
        REQUIRE (r.beats[i].tick == (Tick) i * 960);   // consecutive, none twice
    for (size_t i = 1; i < r.beats.size(); ++i)
        REQUIRE (r.beats[i].sample > r.beats[i - 1].sample);
}

TEST_CASE ("Loop wraps to the loop start and re-fires its first beat", "[transport][loop]")
{
    Runner r (48000.0, 120.0);
    r.transport.setLoop (true, 0, 3840);      // one bar = 96000 samples
    r.run (96000 * 3, 1000);                  // wrap happens inside a block

    REQUIRE (r.beats.size() == 12);
    for (size_t i = 0; i < r.beats.size(); ++i)
    {
        REQUIRE (r.beats[i].sample == (std::int64_t) i * 24000);
        REQUIRE (r.beats[i].tick == (Tick) (i % 4) * 960);
        REQUIRE (r.beats[i].beatInBar == (int) (i % 4) + 1);
    }

    REQUIRE (r.transport.getPositionTicks() == Catch::Approx (0.0).margin (1e-6));
}

TEST_CASE ("Loop not starting at zero, with block size 1", "[transport][loop]")
{
    Runner r (48000.0, 120.0);
    r.transport.setLoop (true, 3840, 3840 * 2);   // bar 2 only
    r.run (96000 * 3, 1);

    // Bar 1 plays through, then bar 2 repeats twice.
    REQUIRE (r.beats.size() == 12);
    for (size_t i = 0; i < r.beats.size(); ++i)
    {
        REQUIRE (r.beats[i].sample == (std::int64_t) i * 24000);
        const Tick expected = i < 4 ? (Tick) i * 960 : 3840 + (Tick) ((i - 4) % 4) * 960;
        REQUIRE (r.beats[i].tick == expected);
    }
}

TEST_CASE ("Invalid loops are ignored", "[transport][loop]")
{
    Runner r (48000.0, 120.0);
    r.transport.setLoop (true, 3840, 3840);   // empty
    r.run (96000 * 2, 512);
    REQUIRE (r.beats.size() == 8);
    REQUIRE (r.beats.back().tick == 7 * 960);
}

TEST_CASE ("Rewind returns to tick 0 and replays the downbeat", "[transport]")
{
    Runner r (48000.0, 120.0);
    r.run (50000, 512);
    r.transport.rewind();
    REQUIRE (r.transport.getPositionTicks() == 0.0);

    r.beats.clear();
    r.elapsed = 0;
    r.run (1, 1);
    REQUIRE (r.beats.size() == 1);
    REQUIRE (r.beats[0].tick == 0);
}
