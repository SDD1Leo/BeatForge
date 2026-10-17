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
    // Simulates a device: owns channel buffers, calls process() per block and
    // keeps a recording of the first channel.
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
                recording.insert (recording.end(), buffers[0].begin(), buffers[0].end());
                for (auto& b : buffers)
                    for (auto s : b)
                        maxAbs = std::max (maxAbs, std::abs (s));
            }
            return maxAbs;
        }

        // Sample indices where a click starts (silence followed by sound).
        std::vector<size_t> clickOnsets() const
        {
            std::vector<size_t> onsets;
            size_t silentRun = 1000;
            for (size_t i = 0; i < recording.size(); ++i)
            {
                if (recording[i] != 0.0f && silentRun >= 100)
                    onsets.push_back (i - 1);   // the click's first sample is sin(0) == 0
                silentRun = recording[i] == 0.0f ? silentRun + 1 : 0;
            }
            return onsets;
        }

        AudioEngine& engine;
        int blockSize;
        std::vector<std::vector<float>> buffers;
        std::vector<float*> pointers;
        std::vector<float> recording;
    };
}

TEST_CASE ("Stopped engine outputs exact silence and clears garbage", "[audio][engine]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 256, 4);   // buffers start filled with 7.0
    REQUIRE (device.runBlocks (10) == 0.0f);
    REQUIRE (engine.getState() == TransportState::stopped);
    REQUIRE (engine.getPositionTicks() == 0.0);
}

TEST_CASE ("Metronome is off by default", "[audio][engine][metronome]")
{
    AudioEngine engine;
    REQUIRE_FALSE (engine.isMetronomeEnabled());
    FakeDevice device (engine, 48000.0, 512);
    engine.play();
    REQUIRE (device.runBlocks (200) == 0.0f);   // empty pattern, no clicks: silence
    REQUIRE (engine.getBeatCount() == 5);
}

TEST_CASE ("Metronome clicks are heard exactly on the beats", "[audio][engine][metronome]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (true);
    FakeDevice device (engine, 48000.0, 512);
    engine.play();
    device.runBlocks (48000 * 4 / 512);   // 4 s at 120 BPM

    const auto onsets = device.clickOnsets();
    REQUIRE (onsets.size() == 8);
    for (size_t i = 0; i < onsets.size(); ++i)
        REQUIRE (onsets[i] == i * 24000);

    // Downbeats (beats 1, 5) are louder.
    auto clickPeak = [&] (size_t at) {
        float p = 0.0f;
        for (size_t i = at; i < at + 1440; ++i) p = std::max (p, std::abs (device.recording[i]));
        return p; };
    REQUIRE (clickPeak (onsets[0]) > clickPeak (onsets[1]) * 1.4f);
    REQUIRE (clickPeak (onsets[4]) > clickPeak (onsets[5]) * 1.4f);
}

TEST_CASE ("Tempo set from another thread applies at the next block", "[audio][engine][tempo]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (true);
    engine.setTempo (100.0);
    FakeDevice device (engine, 44100.0, 441);
    engine.play();
    device.runBlocks (300);   // 3 s

    const auto onsets = device.clickOnsets();
    REQUIRE (onsets.size() == 5);
    for (size_t i = 0; i < onsets.size(); ++i)
        REQUIRE (onsets[i] == i * 26460);

    engine.setTempo (500.0);
    REQUIRE (engine.getTempo() == 240.0);   // clamped
}

TEST_CASE ("Metronome can be muted while the clock keeps running", "[audio][engine][metronome]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (false);
    FakeDevice device (engine, 48000.0, 480);
    engine.play();
    REQUIRE (device.runBlocks (300) == 0.0f);
    REQUIRE (engine.getBeatCount() == 6);
    REQUIRE (engine.getPositionTicks() == Catch::Approx (300 * 480 / 25.0));
}

TEST_CASE ("Stop rewinds at once and lets the click ring out", "[audio][engine]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (true);
    FakeDevice device (engine, 48000.0, 128);
    engine.play();
    device.runBlocks (2);   // inside the first click
    engine.stop();
    REQUIRE (device.runBlocks (1) > 0.0f);   // tail still sounding
    REQUIRE (engine.getState() == TransportState::stopped);
    REQUIRE (engine.getPositionTicks() == 0.0);

    device.runBlocks (20);  // 30 ms click < 23 * 128 samples
    REQUIRE (device.runBlocks (5) == 0.0f);

    // Next play starts again from the downbeat.
    device.recording.clear();
    engine.play();
    device.runBlocks (1);
    REQUIRE (engine.getBeatCount() == 2);
    REQUIRE (device.clickOnsets() == std::vector<size_t> { 0 });
}

TEST_CASE ("Loop region in the engine wraps the musical position", "[audio][engine][loop]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (true);
    engine.setLoop (true, 0, 3840);   // 1 bar @ 120 BPM = 2 s
    FakeDevice device (engine, 48000.0, 1000);
    engine.play();
    device.runBlocks (240);           // 5 s
    REQUIRE (engine.getPositionTicks() == Catch::Approx (960.0 * 2).margin (1e-6));   // 1 s into the bar
    REQUIRE (device.clickOnsets().size() == 10);
}

TEST_CASE ("Repeated play/stop stays well-formed", "[audio][engine]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (true);
    FakeDevice device (engine, 48000.0, 128);

    for (int cycle = 0; cycle < 500; ++cycle)
    {
        engine.play();
        const float playing = device.runBlocks (1 + cycle % 7);
        engine.stop();
        device.runBlocks (1 + cycle % 3);

        REQUIRE (std::isfinite (playing));
        REQUIRE (playing <= 0.5f);
    }

    device.runBlocks (30);
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
    REQUIRE (engine.getPositionTicks() == 0.0);

    FakeDevice device (engine, 96000.0, 64);
    engine.setMetronomeEnabled (true);
    REQUIRE (engine.getSampleRate() == 96000.0);
    REQUIRE (device.runBlocks (4) == 0.0f);

    engine.play();
    device.runBlocks (96000 / 64);
    const auto onsets = device.clickOnsets();
    REQUIRE (onsets.size() == 2);
    REQUIRE (onsets[1] - onsets[0] == 48000);
}

TEST_CASE ("Output meter sees the clicks", "[audio][engine][meter]")
{
    AudioEngine engine;
    engine.setMetronomeEnabled (true);
    FakeDevice device (engine, 48000.0, 480);
    engine.play();
    device.runBlocks (20);

    for (int ch = 0; ch < 2; ++ch)
        REQUIRE (LevelMeter::toDecibels (engine.getOutputMeter().takePeak (ch)) > -8.0f);
}

TEST_CASE ("Audio callback path never allocates", "[audio][engine][realtime]")
{
    AudioEngine engine;
    FakeDevice device (engine, 48000.0, 256);   // allocates here, outside the guard
    device.recording.reserve (1 << 20);

    for (int step = 0; step < 16; ++step)
        for (int lane = 0; lane < DrumMachine::numLanes; ++lane)
            engine.getDrumMachine().setStep (lane, step, (step + lane) % 3 == 0);

    beatforge::test::ScopedAllocationCounter allocations;
    engine.setMetronomeEnabled (true);
    engine.getDrumMachine().preview (2);
    engine.play();
    device.runBlocks (100);
    engine.getDrumMachine().setLaneMuted (1, true);
    engine.getDrumMachine().setLaneSoloed (0, true);
    engine.getDrumMachine().setLaneGainDb (0, -6.0f);
    engine.setTempo (90.0);
    engine.setTimeSignature ({ 7, 8 });
    engine.setLoop (true, 0, 3360);
    device.runBlocks (200);
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
