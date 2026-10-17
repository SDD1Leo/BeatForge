#include "AllocationGuard.h"

#include <beatforge/audio/AudioEngine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace beatforge::audio;

namespace
{
    // Simulates a device: owns channel buffers and calls process() per block.
    struct FakeDevice
    {
        FakeDevice (AudioEngine& e, double rate, int block, int channels = 2)
            : engine (e), blockSize (block), buffers ((size_t) channels, std::vector<float> ((size_t) block, 7.0f))
        {
            for (auto& b : buffers)
                pointers.push_back (b.data());
            engine.prepare (rate);
        }

        float runBlocks (int numBlocks)
        {
            float maxAbs = 0.0f;
            for (int i = 0; i < numBlocks; ++i)
            {
                engine.process (pointers.data(), (int) pointers.size(), blockSize);
                for (auto& b : buffers)
                    for (auto s : b)
                        maxAbs = std::max (maxAbs, std::abs (s));
            }
            return maxAbs;
        }

        AudioEngine& engine;
        int blockSize;
        std::vector<std::vector<float>> buffers;
        std::vector<float*> pointers;
    };
}

TEST_CASE ("Stopped engine outputs exact silence and clears garbage", "[audio][engine]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 256, 4);   // buffers start filled with 7.0
    REQUIRE (device.runBlocks (10) == 0.0f);
    REQUIRE (engine.getState() == TransportState::stopped);
    REQUIRE (engine.getPositionSamples() == 0);
}

TEST_CASE ("Play produces audio on the first stereo pair only", "[audio][engine]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 512, 4);
    engine.play();
    device.runBlocks (5);

    REQUIRE (engine.getState() == TransportState::playing);
    auto blockPeak = [] (const std::vector<float>& b) {
        float p = 0.0f; for (auto s : b) p = std::max (p, std::abs (s)); return p; };
    REQUIRE (blockPeak (device.buffers[0]) > 0.1f);
    REQUIRE (device.buffers[0] == device.buffers[1]);
    REQUIRE (blockPeak (device.buffers[2]) == 0.0f);
    REQUIRE (blockPeak (device.buffers[3]) == 0.0f);
}

TEST_CASE ("Position advances only while playing and rewinds after stop", "[audio][engine]")
{
    AudioEngine engine;
    FakeDevice device (engine, 44100.0, 147);   // 3.33 ms blocks, shorter than the 10 ms fade

    engine.play();
    device.runBlocks (300);
    REQUIRE (engine.getPositionSamples() == 44100);   // exactly one second

    engine.stop();
    device.runBlocks (1);
    REQUIRE (engine.getState() == TransportState::stopping);
    REQUIRE (engine.getPositionSamples() == 44100);   // frozen during fade

    device.runBlocks (3);                              // 441-sample fade done within 4 blocks
    REQUIRE (engine.getState() == TransportState::stopped);
    REQUIRE (engine.getPositionSamples() == 0);
    REQUIRE (device.runBlocks (5) == 0.0f);
}

TEST_CASE ("Repeated play/stop stays well-formed", "[audio][engine]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 128);

    for (int cycle = 0; cycle < 500; ++cycle)
    {
        engine.play();
        const float playing = device.runBlocks (1 + cycle % 7);
        engine.stop();
        device.runBlocks (1 + cycle % 3);

        REQUIRE (std::isfinite (playing));
        REQUIRE (playing <= 0.126f);   // never louder than -18 dBFS (+ rounding)
    }

    device.runBlocks (10);
    REQUIRE (engine.getState() == TransportState::stopped);
    REQUIRE (device.runBlocks (1) == 0.0f);
}

TEST_CASE ("Changing sample rate (device restart) keeps the engine usable", "[audio][engine]")
{
    AudioEngine engine;
    {
        FakeDevice device (engine, 44100.0, 512);
        engine.play();
        device.runBlocks (10);
    }

    engine.reset();   // device stopped
    REQUIRE (engine.getState() == TransportState::stopped);
    REQUIRE_FALSE (engine.isPlayRequested());

    FakeDevice device (engine, 96000.0, 64);
    REQUIRE (engine.getSampleRate() == 96000.0);
    REQUIRE (device.runBlocks (4) == 0.0f);

    engine.play();
    REQUIRE (device.runBlocks (100) > 0.1f);
}

TEST_CASE ("Output meter sees the -18 dBFS tone", "[audio][engine][meter]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 480);
    engine.play();
    device.runBlocks (20);

    auto& meter = engine.getOutputMeter();
    for (int ch = 0; ch < 2; ++ch)
        REQUIRE (LevelMeter::toDecibels (meter.takePeak (ch)) == Catch::Approx (-18.0f).margin (0.05f));

    REQUIRE (meter.takePeak (0) == 0.0f);   // take resets
}

TEST_CASE ("Audio callback path never allocates", "[audio][engine][realtime]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 256);   // allocates here, outside the guard

    beatforge::test::ScopedAllocationCounter allocations;
    engine.play();
    device.runBlocks (50);
    engine.stop();
    device.runBlocks (50);
    (void) engine.getOutputMeter().takePeak (0);

    REQUIRE (allocations.count() == 0);
}

TEST_CASE ("Allocation counter actually detects allocations", "[realtime]")
{
    beatforge::test::ScopedAllocationCounter allocations;
    auto* p = new int (3);
    asm volatile ("" : : "g" (p) : "memory");   // keep the optimiser from eliding new/delete
    delete p;
    REQUIRE (allocations.count() == 1);
}
