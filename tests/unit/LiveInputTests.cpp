#include "AllocationGuard.h"
#include "EngineHelpers.h"

#include <beatforge/audio/AudioEngine.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace beatforge;
using beatforge::test::peakIn;
using Catch::Approx;
using Recorded = audio::AudioEngine::RecordedEvent;

namespace
{
    // The engine reads this clock once per block; tests move it in step with the audio.
    double fakeNow = 0.0;
    double fakeClock() noexcept { return fakeNow; }

    constexpr double sr = 48000.0;

    struct LiveRig
    {
        audio::AudioEngine engine;
        int blockSize;
        std::int64_t rendered = 0;
        std::vector<float> out;

        explicit LiveRig (int block = 480) : blockSize (block)
        {
            fakeNow = 0.0;
            engine.prepare (sr);
            engine.setClockForTesting (&fakeClock);
            engine.setSlotType (0, audio::SlotType::instrument);
            engine.setSlotSchedule (0, nullptr);
            engine.setLiveTarget (0);
        }

        ~LiveRig() { engine.setClockForTesting (nullptr); }

        // Renders up to `untilSample`; the clock reads the time of each block's end
        // (a callback runs once the previous block's worth of time has passed).
        void renderTo (std::int64_t untilSample)
        {
            std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);
            float* ch[] { l.data(), r.data() };
            while (rendered < untilSample)
            {
                fakeNow = (double) rendered / sr;
                engine.process (ch, 2, blockSize);
                out.insert (out.end(), l.begin(), l.end());
                rendered += blockSize;
            }
        }

        void key (double time, int pitch, int velocity) { REQUIRE (engine.deviceNote ({ time, pitch, velocity })); }

        std::vector<Recorded> recorded()
        {
            std::vector<Recorded> all;
            Recorded e;
            while (engine.popRecorded (e))
                all.push_back (e);
            return all;
        }
    };

    size_t firstSound (const std::vector<float>& x)
    {
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != 0.0f) return i;
        return x.size();
    }
}

TEST_CASE ("Live notes sound one block after they arrive, keeping their spacing", "[live]")
{
    for (int block : { 480, 256, 1024 })
    {
        LiveRig rig (block);
        rig.key (0.0125, 60, 100);              // arrives 12.5 ms in
        rig.renderTo (48000);

        // Sample 600 + one block; the first sample of a note may still be exactly 0.
        const auto onset = firstSound (rig.out);
        INFO ("block " << block);
        REQUIRE (onset >= (size_t) (600 + block));
        REQUIRE (onset <= (size_t) (600 + block + 1));
        REQUIRE (peakIn (rig.out, (size_t) (600 + block), (size_t) (600 + block + 480)) > 0.01f);
    }
}

TEST_CASE ("Live notes need a synth target", "[live]")
{
    SECTION ("no target")
    {
        LiveRig rig;
        rig.engine.setLiveTarget (-1);
        rig.key (0.0, 60, 100);
        rig.renderTo (24000);
        REQUIRE (peakIn (rig.out) == 0.0f);
    }
    SECTION ("a drum slot")
    {
        LiveRig rig;
        rig.engine.setSlotType (1, audio::SlotType::drums);
        rig.engine.setLiveTarget (1);
        rig.key (0.0, 60, 100);
        rig.renderTo (24000);
        REQUIRE (peakIn (rig.out) == 0.0f);
    }
}

TEST_CASE ("Note off, all-notes-off and a new target release live notes", "[live]")
{
    LiveRig rig;
    rig.key (0.0, 60, 100);
    rig.key (0.0, 64, 100);
    rig.renderTo (4800);
    REQUIRE (rig.engine.getActiveVoices (0) == 2);

    SECTION ("note off")
    {
        rig.key (0.1, 60, 0);
        rig.renderTo (48000);                    // 0.9 s: past the 250 ms release
        REQUIRE (rig.engine.getActiveVoices (0) == 1);
    }
    SECTION ("all notes off")
    {
        rig.key (0.1, -1, 0);
        rig.renderTo (48000);
        REQUIRE (rig.engine.getActiveVoices (0) == 0);
    }
    SECTION ("target changes")
    {
        rig.engine.setSlotType (1, audio::SlotType::instrument);
        rig.engine.setLiveTarget (1);
        rig.renderTo (48000);
        REQUIRE (rig.engine.getActiveVoices (0) == 0);
        rig.key (1.0, 67, 100);                  // now plays on the new target
        rig.renderTo (60000);
        REQUIRE (rig.engine.getActiveVoices (1) == 1);
    }
    SECTION ("control: held notes keep sounding")
    {
        rig.renderTo (48000);
        REQUIRE (rig.engine.getActiveVoices (0) == 2);
    }
}

TEST_CASE ("Held live notes survive play, seek, loop wraps, clip edits and stop", "[live]")
{
    LiveRig rig;
    test::playNotes (rig.engine, 0, { { 0, 48, 100, 0, 960 } }, 3840);   // loops bar 1
    rig.key (0.0, 72, 100);
    rig.renderTo (4800);

    rig.engine.play();
    rig.renderTo (48000 * 3);                     // two loop wraps (bar = 2 s)
    rig.engine.seek (1920.0);
    test::playNotes (rig.engine, 0, { { 0, 50, 100, 0, 960 } }, 3840);   // schedule change
    rig.renderTo (48000 * 4);
    rig.engine.stop();
    rig.renderTo (48000 * 5);

    // The clip's notes ended (release done), the held key still sounds.
    REQUIRE (rig.engine.getActiveVoices (0) == 1);
}

TEST_CASE ("Recording reports where the player heard the note", "[live][record]")
{
    // 120 BPM at 48 kHz: 25 samples per tick. A key pressed at 0.1 s is pressed
    // with the beat at tick 192 (no output latency): that is what gets recorded,
    // for any block size.
    for (int block : { 480, 256 })
    {
        LiveRig rig (block);
        rig.engine.setRecording (true);
        rig.engine.play();
        rig.key (0.1, 60, 90);
        rig.key (0.3, 60, 0);
        rig.renderTo (48000);
        rig.engine.stop();
        rig.renderTo (48000 + 2 * block);

        const auto events = rig.recorded();
        INFO ("block " << block);
        REQUIRE (events.size() == 3);
        REQUIRE (events[0].pitch == 60);
        REQUIRE (events[0].velocity == 90);
        REQUIRE (events[0].tick == Approx (192.0).margin (0.05));
        REQUIRE (events[1].velocity == 0);
        REQUIRE (events[1].tick == Approx (576.0).margin (0.05));
        REQUIRE (events[2].pitch == Recorded::endOfTake);
        REQUIRE (events[2].tick > 576.0);
    }
}

TEST_CASE ("Output latency is compensated, also across a loop wrap", "[live][record]")
{
    LiveRig rig;
    rig.engine.setLoop (true, 0, 3840);           // 1 bar = 96000 samples
    rig.engine.setOutputLatency (2400);           // 50 ms from rendering to the speakers = 96 ticks
    rig.engine.setRecording (true);
    rig.engine.play();

    // Pressed 30 ticks after the wrap (sample 96750), while hearing the beat 96
    // ticks earlier, which was before the wrap: tick 3840 - 66.
    rig.key (96750.0 / sr, 62, 100);
    rig.renderTo (100000);

    const auto events = rig.recorded();
    REQUIRE (events.size() == 1);
    REQUIRE (events[0].tick == Approx (3774.0).margin (0.05));
}

TEST_CASE ("Punching out ends the take; later notes are not recorded", "[live][record]")
{
    LiveRig rig;
    rig.engine.setOutputLatency (480);
    rig.engine.setRecording (true);
    rig.engine.play();
    rig.key (0.1, 60, 100);
    rig.renderTo (24000);
    rig.engine.setRecording (false);              // at 0.5 s (tick 960)
    rig.renderTo (24480);
    rig.key (0.6, 62, 100);
    rig.renderTo (48000);

    const auto events = rig.recorded();
    REQUIRE (events.size() == 2);
    REQUIRE (events[0].pitch == 60);
    REQUIRE (events[1].pitch == Recorded::endOfTake);
    REQUIRE (events[1].tick == Approx (960.0 - 19.2).margin (0.05));   // compensated like the notes
    REQUIRE (rig.engine.getActiveVoices (0) == 2);                       // still heard live
}

TEST_CASE ("Bursty audio callbacks do not shift live notes", "[live][record]")
{
    // A sound server that pulls two blocks back to back: callbacks at 0, 0,
    // 2b, 2b, 4b, 4b... Notes 0.6 s apart must still sound and record exactly
    // 0.6 s apart (gaps long enough for each release to end in silence).
    audio::AudioEngine engine;
    engine.prepare (sr);
    engine.setClockForTesting (&fakeClock);
    engine.setSlotType (0, audio::SlotType::instrument);
    engine.setSlotSchedule (0, nullptr);
    engine.setLiveTarget (0);
    engine.setRecording (true);
    engine.play();

    const int block = 512;
    const double times[] { 0.1003, 0.7003, 1.3003, 1.9003 };
    for (double t : times)
    {
        engine.deviceNote ({ t, 60, 100 });
        engine.deviceNote ({ t + 0.05, 60, 0 });
    }

    std::vector<float> l (block), r (block), out;
    float* ch[] { l.data(), r.data() };
    for (int k = 0; k < 220; ++k)
    {
        fakeNow = (double) ((k / 2) * 2 * block) / sr;
        engine.process (ch, 2, block);
        out.insert (out.end(), l.begin(), l.end());
    }
    engine.setClockForTesting (nullptr);

    std::vector<double> ons;
    Recorded e;
    while (engine.popRecorded (e))
        if (e.velocity > 0) ons.push_back (e.tick);
    REQUIRE (ons.size() == 4);
    for (size_t i = 0; i < ons.size(); ++i)
        REQUIRE (ons[i] == Approx (times[i] * sr / 25.0).margin (0.05));

    // Audio: every note starts the same delay after its key press.
    std::vector<size_t> onsets;
    for (size_t i = 1; i < out.size(); ++i)
        if (out[i] != 0.0f && out[i - 1] == 0.0f && (onsets.empty() || i - onsets.back() > 4800))
            onsets.push_back (i);
    REQUIRE (onsets.size() == 4);
    for (size_t i = 1; i < onsets.size(); ++i)
    {
        INFO ("onsets " << onsets[0] << " " << onsets[1] << " " << onsets[2] << " " << onsets[3]);
        REQUIRE ((double) (onsets[i] - onsets[i - 1]) == Approx (28800.0).margin (1.0));
    }
}

TEST_CASE ("Nothing is recorded while stopped or not recording", "[live][record]")
{
    LiveRig rig;
    rig.engine.setRecording (true);
    rig.key (0.1, 60, 100);                       // stopped
    rig.renderTo (24000);
    rig.engine.setRecording (false);
    rig.engine.play();
    rig.key (0.6, 62, 100);                       // playing, not recording
    rig.renderTo (48000);
    REQUIRE (rig.recorded().empty());
}

TEST_CASE ("Live input and recording are allocation-free on the audio thread", "[live][realtime]")
{
    LiveRig rig;
    rig.engine.setRecording (true);
    rig.engine.play();
    rig.renderTo (4800);

    for (int i = 0; i < 40; ++i)
        rig.key (0.1 + i * 0.01, 40 + i, i % 2 == 0 ? 100 : 0);
    rig.engine.deviceNote ({ 0.6, -1, 0 });
    rig.engine.screenNote ({ 0.62, 70, 100 });

    std::vector<float> l (480), r (480);
    float* ch[] { l.data(), r.data() };
    test::ScopedAllocationCounter allocations;
    for (int b = 0; b < 200; ++b)
    {
        fakeNow = (double) (4800 + b * 480) / sr;
        rig.engine.process (ch, 2, 480);
        if (b == 100) rig.engine.setLiveTarget (-1);
    }
    REQUIRE (allocations.count() == 0);
}

TEST_CASE ("A key pressed as the take starts lands on its first beat, not at the loop end", "[live][record]")
{
    // The bug: with a loop, a press just ahead of the first beat was moved back
    // by the output latency and wrapped to the end of the loop (or past it).
    LiveRig rig;
    rig.engine.setLoop (true, 0, 3840);
    rig.engine.setOutputLatency (2400);
    rig.engine.setRecording (true);
    rig.engine.play();
    rig.key (0.0, 57, 100);                      // together with the start of recording
    rig.key (0.9, 57, 0);
    rig.key (2.0 + 0.010, 60, 100);              // early in the second pass: wraps, as it should
    rig.key (2.3, 60, 0);
    rig.renderTo (130000);

    const auto events = rig.recorded();
    REQUIRE (events.size() == 4);
    REQUIRE (events[0].pitch == 57);
    REQUIRE (events[0].tick == Approx (0.0).margin (0.05));
    REQUIRE (events[1].tick < 3840.0);
    // Second pass: pressed 10 ms after the wrap, heard 50 ms earlier = before the wrap.
    REQUIRE (events[2].tick > 3700.0);
    REQUIRE (events[2].tick < 3840.0);
}
