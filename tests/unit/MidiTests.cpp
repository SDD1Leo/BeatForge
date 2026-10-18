#include "AllocationGuard.h"

#include <beatforge/audio/AudioEngine.h>
#include "EngineHelpers.h"

#include <beatforge/project/ScheduleBuilder.h>
#include <beatforge/util/LockFreeExchange.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace beatforge;
using midi::Note;

namespace
{
    std::vector<float> render (audio::AudioEngine& engine, int numSamples, int blockSize = 512)
    {
        std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize), out;
        float* chans[] { l.data(), r.data() };
        out.reserve ((size_t) numSamples);
        for (int done = 0; done < numSamples; done += blockSize)
        {
            const int n = std::min (blockSize, numSamples - done);
            engine.process (chans, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }

    float peak (const std::vector<float>& x, size_t from, size_t to)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }

    size_t firstNonZero (const std::vector<float>& x, size_t from = 0)
    {
        for (size_t i = from; i < x.size(); ++i)
            if (x[i] != 0.0f) return i;
        return x.size();
    }

    midi::Clip clipWith (std::vector<Note> notes, time::Tick length = 3840)
    {
        midi::NoteId id = 1;
        for (auto& n : notes) n.id = id++;
        return { length, notes };
    }

    // 120 BPM @ 48 kHz: 25 samples per tick, a beat is 24000 samples.
    constexpr int samplesPerTick = 25;
    // Default Nebula Poly release, plus a little for the filter to ring out.
    const int releaseSamples = (int) (audio::defaultValue (audio::InstrumentType::polySynth, audio::Param::release) * 48000.0) + 64;

    audio::PolySynthInstrument makeSynth (float cutoff = 16000.0f, dsp::Waveform wave = dsp::Waveform::saw)
    {
        audio::ParamBlock params (audio::InstrumentType::polySynth);
        params.set (audio::Param::cutoff, cutoff);
        params.set (audio::Param::wave, (float) wave);
        params.set (audio::Param::envAmount, 0.0f);
        audio::PolySynthInstrument synth;
        synth.prepare (48000.0);
        synth.setParams (audio::BlockParams::from (params));
        return synth;
    }
}

//==============================================================================
TEST_CASE ("Quantize moves starts to the nearest grid line", "[midi][quantize]")
{
    std::vector<Note> notes { { 1, 60, 100, 100, 50 }, { 2, 62, 100, 130, 50 }, { 3, 64, 100, 120, 50 }, { 4, 65, 100, 3830, 30 } };
    midi::quantize (notes, midi::sixteenth, 3840);
    REQUIRE (notes[0].start == 0);      // 100 -> 0
    REQUIRE (notes[1].start == 240);    // 130 -> 240
    REQUIRE (notes[2].start == 240);    // exactly halfway: later line
    REQUIRE (notes[3].start == 3600);   // would be 3840 (outside the clip): last line inside
    REQUIRE (notes[0].length == 50);    // lengths kept
    REQUIRE (notes[3].length == 30);
}

TEST_CASE ("Quantize grid sizes and selections", "[midi][quantize]")
{
    const auto grid = GENERATE (midi::quarter, midi::eighth, midi::sixteenth, midi::thirtySecond);
    CAPTURE (grid);
    std::vector<Note> notes;
    for (int i = 0; i < 40; ++i)
        notes.push_back ({ (midi::NoteId) i + 1, 60, 100, (time::Tick) (i * 97 % 3800), 60 });

    midi::quantize (notes, grid, 3840);
    for (auto& n : notes)
    {
        REQUIRE (n.start % grid == 0);
        REQUIRE (n.end() <= 3840);
    }

    std::vector<Note> partial { { 1, 60, 100, 50, 10 }, { 2, 60, 100, 50, 10 } };
    midi::quantize (partial, grid, 3840, { 2 });
    REQUIRE (partial[0].start == 50);   // not selected
    REQUIRE (partial[1].start == 0);
}

TEST_CASE ("Notes are constrained to valid ranges and the clip", "[midi]")
{
    const auto n = midi::constrained ({ 1, 200, 0, 3800, 500 }, 3840);
    REQUIRE (n.pitch == 127);
    REQUIRE (n.velocity == 1);
    REQUIRE (n.start == 3800);
    REQUIRE (n.length == 40);
    REQUIRE (midi::constrained ({ 1, -5, 300, -10, 0 }, 3840) == Note { 1, 0, 127, 0, 1 });
    REQUIRE (midi::pitchToFrequency (69) == Catch::Approx (440.0));
    REQUIRE (midi::pitchToFrequency (60) == Catch::Approx (261.6256).epsilon (1e-5));
}

TEST_CASE ("Schedules order events and cut notes at clip ends", "[midi][schedule]")
{
    project::Track t;
    t.type = project::TrackType::instrument;
    project::Clip a;
    a.id = 7;
    a.start = 0;
    a.length = 3840;
    a.notes = { { 1, 60, 90, 480, 480 },     // off at 960
                { 2, 64, 80, 0, 3840 },      // whole clip
                { 3, 67, 70, 960, 240 },     // on at 960 (after the off)
                { 4, 72, 70, 4000, 240 } };  // beyond the clip: kept, silent
    project::Clip b = a;
    b.id = 8;
    b.start = 3840 + 1920;                   // second clip starts half a bar after the first ends
    t.clips = { a, b };

    const auto s = project::buildSchedule (t);
    REQUIRE (s->spans.size() == 6);
    REQUIRE (s->events.size() == 12);
    REQUIRE ((s->events[0].tick == 0 && s->events[0].kind == audio::TrackSchedule::Kind::noteOn && s->events[0].pitch == 64));
    REQUIRE ((s->events[1].tick == 480 && s->events[1].pitch == 60));
    REQUIRE ((s->events[2].tick == 960 && s->events[2].kind == audio::TrackSchedule::Kind::noteOff));   // offs before ons
    REQUIRE ((s->events[3].tick == 960 && s->events[3].kind == audio::TrackSchedule::Kind::noteOn));
    REQUIRE (s->covers (project::voiceId (7, 2), 3839.0));
    REQUIRE_FALSE (s->covers (project::voiceId (7, 2), 3840.0));             // cut at the clip end
    REQUIRE (s->covers (project::voiceId (8, 2), 5760.0));                   // the copy in the second clip
    REQUIRE_FALSE (s->covers (project::voiceId (7, 4), 4000.0));
}

TEST_CASE ("A later clip takes over from an overlapping one", "[midi][schedule]")
{
    project::Track t;
    t.type = project::TrackType::instrument;
    project::Clip a;
    a.id = 1;
    a.length = 7680;
    a.notes = { { 1, 60, 100, 0, 7680 } };
    project::Clip b;
    b.id = 2;
    b.start = 3840;
    b.length = 3840;
    b.notes = { { 1, 67, 100, 0, 240 } };
    t.clips = { a, b };

    const auto s = project::buildSchedule (t);
    REQUIRE_FALSE (s->covers (project::voiceId (1, 1), 3840.0));   // a's long note ends where b starts
    REQUIRE (s->covers (project::voiceId (2, 1), 3840.0));
}

TEST_CASE ("Drum clips repeat their pattern every bar", "[midi][schedule]")
{
    project::Track t;
    t.type = project::TrackType::drums;
    project::Clip c;
    c.id = 1;
    c.start = 3840;
    c.length = 3 * 3840 + 960;   // 3.25 bars
    c.pattern[0][0] = c.pattern[0][8] = c.pattern[1][4] = true;
    t.clips = { c };

    const auto s = project::buildSchedule (t);
    std::vector<time::Tick> kicks, snares;
    for (auto& e : s->events)
    {
        if (e.laneMask & 1) kicks.push_back (e.tick);
        if (e.laneMask & 2) snares.push_back (e.tick);
    }
    REQUIRE (kicks == std::vector<time::Tick> { 3840, 3840 + 1920, 7680, 7680 + 1920, 11520, 11520 + 1920, 15360 });
    REQUIRE (snares == std::vector<time::Tick> { 3840 + 960, 7680 + 960, 11520 + 960 });   // the 4th bar is cut before beat 2
}

TEST_CASE ("Lock-free exchange hands over without losing or leaking", "[midi][schedule][realtime]")
{
    util::LockFreeExchange<std::vector<int>> exchange;
    bool changed = false;
    REQUIRE (exchange.acquire (changed) == nullptr);
    REQUIRE_FALSE (changed);

    exchange.publish (std::make_unique<std::vector<int>> (1, 1));
    REQUIRE (exchange.acquire (changed)->size() == 1);
    REQUIRE (changed);
    REQUIRE (exchange.acquire (changed)->size() == 1);
    REQUIRE_FALSE (changed);

    // Two publishes before the audio thread looks: only the newest is used.
    exchange.publish (std::make_unique<std::vector<int>> (2, 2));
    exchange.publish (std::make_unique<std::vector<int>> (3, 3));
    REQUIRE (exchange.acquire (changed)->size() == 3);
    exchange.collectGarbage();

    {
        beatforge::test::ScopedAllocationCounter allocations;
        for (int i = 0; i < 100; ++i)
            exchange.acquire (changed);
        REQUIRE (allocations.count() == 0);
    }
}

//==============================================================================
TEST_CASE ("Synth plays the right pitch at the right level", "[midi][synth]")
{
    auto synth = makeSynth();
    synth.noteOn (1, 69, 127);

    std::vector<float> out (48000, 0.0f);
    float* ch[] { out.data() };
    synth.render (ch, 1, 0, 48000);

    int crossings = 0;
    for (size_t i = 24001; i < 48000; ++i)
        if (out[i - 1] < 0.0f && out[i] >= 0.0f) ++crossings;
    REQUIRE (std::abs (crossings - 220) <= 1);   // 440 Hz over 0.5 s

    REQUIRE (peak (out, 0, 48000) < 0.5f);    // headroom for chords
    REQUIRE (peak (out, 0, 48000) > 0.05f);
}

TEST_CASE ("Velocity scales the level; release fades to silence", "[midi][synth]")
{
    auto level = [] (int velocity)
    {
        auto synth = makeSynth();
        synth.noteOn (1, 60, velocity);
        std::vector<float> out (9600, 0.0f);
        float* ch[] { out.data() };
        synth.render (ch, 1, 0, 9600);
        return peak (out, 0, 9600);
    };
    REQUIRE (level (64) == Catch::Approx (level (127) * 64.0f / 127.0f).epsilon (0.01));

    auto synth = makeSynth();
    synth.noteOn (1, 60, 100);
    std::vector<float> out (48000, 0.0f);
    float* ch[] { out.data() };
    synth.render (ch, 1, 0, 4800);
    synth.noteOff (1);
    synth.render (ch, 1, 4800, 43200);
    REQUIRE (peak (out, 4800 + (size_t) releaseSamples, 48000) < 1e-4f);
    REQUIRE (synth.activeVoices() == 0);
}

TEST_CASE ("Overlapping notes of the same pitch release by id", "[midi][synth]")
{
    auto synth = makeSynth();
    synth.noteOn (1, 60, 100);
    synth.noteOn (2, 60, 100);
    REQUIRE (synth.activeVoices() == 2);
    synth.noteOff (1);

    std::vector<float> out (48000, 0.0f);
    float* ch[] { out.data() };
    synth.render (ch, 1, 0, 48000);
    REQUIRE (synth.activeVoices() == 1);   // note 2 still holds
}

TEST_CASE ("Voice stealing keeps polyphony bounded", "[midi][synth]")
{
    auto synth = makeSynth();
    for (int i = 0; i < 40; ++i)
        synth.noteOn ((midi::NoteId) i + 1, 40 + i, 100);
    REQUIRE (synth.activeVoices() == audio::PolySynthInstrument::numVoices);
}

//==============================================================================
TEST_CASE ("A note starts and stops on its exact samples", "[midi][engine]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 69, 100, 960, 480 } });   // beat 2, an eighth long
    beatforge::test::playNotes (engine, 0, clip.notes, clip.length);
    engine.play();
    const auto out = render (engine, 96000);

    // Silent until the note-on at 24000; the first rendered sample of a saw from
    // phase 0 is exactly 0, the next is not.
    REQUIRE (firstNonZero (out) == 24000 + 1);

    // Same note but longer: identical until the note-off at 36000, different after.
    audio::AudioEngine longer;
    longer.prepare (48000.0);
    const auto longClip = clipWith ({ { 0, 69, 100, 960, 960 } });
    beatforge::test::playNotes (longer, 0, longClip.notes, longClip.length);
    longer.play();
    const auto outLong = render (longer, 96000);

    REQUIRE (std::equal (out.begin(), out.begin() + 36000, outLong.begin()));
    REQUIRE (out[36001] != outLong[36001]);
    REQUIRE (peak (out, 36000 + (size_t) releaseSamples, 96000) == 0.0f);   // released, then silent
}

TEST_CASE ("MIDI output is identical for any block size", "[midi][engine]")
{
    const int blockSize = GENERATE (1, 37, 256, 1000);
    CAPTURE (blockSize);

    auto setup = [] (audio::AudioEngine& e, midi::Clip& clip)
    {
        e.prepare (48000.0);
        e.setTempo (131.0);
        clip = clipWith ({ { 0, 60, 100, 0, 240 }, { 0, 64, 90, 240, 700 }, { 0, 67, 80, 1000, 2840 },
                           { 0, 72, 127, 3600, 240 }, { 0, 48, 60, 7, 13 } });
        beatforge::test::playNotes (e, 0, clip.notes, clip.length);
        beatforge::test::playPattern (e, 1, beatforge::test::pattern ({ { 0, { 0, 8 } } }), 16);
        e.setLoop (true, 0, 3840 * 2);
        e.play();
    };

    midi::Clip clipA, clipB;
    audio::AudioEngine a, b;
    setup (a, clipA);
    setup (b, clipB);
    REQUIRE (render (a, 48000 * 6, 4096) == render (b, 48000 * 6, blockSize));
}

TEST_CASE ("The clip loops every clip length", "[midi][engine]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 60, 100, 0, 240 } });   // 1-bar clip, note at bar start
    beatforge::test::playNotes (engine, 0, clip.notes, clip.length);
    engine.play();
    const auto out = render (engine, 96000 * 3);

    for (size_t bar = 0; bar < 3; ++bar)
    {
        const auto at = bar * 96000;
        REQUIRE (firstNonZero (out, at) == at + 1);
        REQUIRE (peak (out, at + 6000 + (size_t) releaseSamples, at + 96000) == 0.0f);   // 240 ticks = 6000 samples
    }
}

TEST_CASE ("Notes stay on the musical grid when the tempo changes", "[midi][engine][tempo]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 60, 100, 1920, 240 } });   // beat 3
    beatforge::test::playNotes (engine, 0, clip.notes, clip.length);
    engine.play();
    auto out = render (engine, 30000, 500);   // 1.25 beats at 120 (tick 1200)
    engine.setTempo (60.0);                   // 50 samples per tick
    auto rest = render (engine, 60000, 500);
    out.insert (out.end(), rest.begin(), rest.end());

    REQUIRE (firstNonZero (out) == (size_t) (30000 + (1920 - 1200) * 50 + 1));
}

TEST_CASE ("Deleting or moving a sounding note releases it", "[midi][engine][stuck]")
{
    for (const bool moveInsteadOfDelete : { false, true })
    {
        CAPTURE (moveInsteadOfDelete);
        audio::AudioEngine engine;
        engine.prepare (48000.0);
        const auto clip = clipWith ({ { 0, 60, 100, 0, 3840 } });   // whole bar
        beatforge::test::playNotes (engine, 0, clip.notes, clip.length);
        engine.play();
        render (engine, 24000);   // half a bar in: sounding
        REQUIRE (engine.getActiveVoices (0) == 1);

        const auto edited = moveInsteadOfDelete ? clipWith ({ { 0, 60, 100, 3000, 240 } }) : clipWith ({});
        beatforge::test::playNotes (engine, 0, edited.notes, edited.length);
        const auto out = render (engine, 24000);
        REQUIRE (peak (out, (size_t) releaseSamples + 512, 24000) == 0.0f);
        REQUIRE (engine.getActiveVoices (0) == 0);
    }
}

TEST_CASE ("Stop releases every note", "[midi][engine][stuck]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 60, 100, 0, 3840 }, { 0, 64, 100, 0, 3840 }, { 0, 67, 100, 0, 3840 } });
    beatforge::test::playNotes (engine, 0, clip.notes, clip.length);
    engine.play();
    render (engine, 12000);
    REQUIRE (engine.getActiveVoices (0) == 3);
    engine.stop();
    const auto out = render (engine, 24000);
    REQUIRE (peak (out, (size_t) releaseSamples + 512, 24000) == 0.0f);
    REQUIRE (engine.getActiveVoices (0) == 0);
}

TEST_CASE ("A loop wrap releases notes that cross the loop end", "[midi][engine][stuck]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 60, 100, 2880, 1920 } }, 3840 * 2);   // beat 4 of bar 1 into bar 2
    beatforge::test::playNotes (engine, 0, clip.notes, clip.length);
    engine.setLoop (true, 0, 3840);                                         // loop bar 1 only
    engine.play();

    const auto out = render (engine, 96000 + 48000);
    REQUIRE (peak (out, 72000 + 1, 96000) > 0.0f);                          // sounding in beat 4
    REQUIRE (peak (out, 96000 + (size_t) releaseSamples, 96000 + 48000) == 0.0f);   // released at the wrap
}

TEST_CASE ("Editor auditions play while stopped", "[midi][engine]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    beatforge::test::playNotes (engine, 0, {}, 3840, false);
    engine.previewNoteOn (0, 60, 100);
    auto held = render (engine, 9600);
    REQUIRE (peak (held, 0, 9600) > 0.05f);
    engine.previewNoteOff (0, 60);
    const auto after = render (engine, 24000);
    REQUIRE (peak (after, (size_t) releaseSamples + 512, 24000) == 0.0f);
    REQUIRE (engine.getState() == audio::TransportState::stopped);
}

TEST_CASE ("Audio thread stays allocation-free with MIDI and clip swaps", "[midi][engine][realtime]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    std::vector<float> l (256), r (256);
    float* chans[] { l.data(), r.data() };

    const auto first = clipWith ({ { 0, 60, 100, 0, 960 }, { 0, 64, 100, 480, 960 }, { 0, 67, 100, 960, 1920 } });
    const auto second = clipWith ({ { 0, 62, 100, 0, 3840 } });
    beatforge::test::playNotes (engine, 0, first.notes, first.length);
    engine.play();

    for (int round = 0; round < 3; ++round)
    {
        {
            beatforge::test::ScopedAllocationCounter allocations;
            engine.previewNoteOn (0, 72, 90);
            for (int i = 0; i < 200; ++i)
                engine.process (chans, 2, 256);
            engine.previewNoteOff (0, 72);
            REQUIRE (allocations.count() == 0);
        }
        // Message thread work (allocates by design): new clip, garbage collection.
        beatforge::test::playNotes (engine, 0, (round % 2 == 0 ? second : first).notes, 3840);
        engine.collectGarbage();
    }
}

TEST_CASE ("Random tempo changes never shift, drop or repeat a note", "[midi][engine][tempo]")
{
    const int blockSize = GENERATE (1, 37, 64, 4096);
    CAPTURE (blockSize);

    // Dense clip: a note on every 32nd with varied lengths, plus drums and a loop.
    midi::Clip clip { 3840 * 2, {} };
    for (int i = 0; i < 64; ++i)
        clip.notes.push_back ({ (midi::NoteId) i + 1, 48 + (i * 7) % 36, 40 + i, (time::Tick) i * 120, (time::Tick) 30 + (i * 53) % 400 });

    auto run = [&] (int bs)
    {
        audio::AudioEngine e;
        e.prepare (44100.0);
        beatforge::test::playNotes (e, 0, clip.notes, clip.length);
        beatforge::test::playPattern (e, 1, beatforge::test::pattern ({ { 2, { 0, 3, 6, 9, 12, 15 } } }), 16);
        e.setLoop (true, 960, 3840 + 1440);
        e.play();

        std::vector<float> l ((size_t) bs), r ((size_t) bs), out;
        float* ch[] { l.data(), r.data() };
        std::uint32_t seed = 12345;
        for (int done = 0; done < 44100 * 8; )
        {
            if (done % 4096 == 0)   // same instants for every block size
            {
                seed = seed * 1664525u + 1013904223u;
                e.setTempo (40.0 + (double) (seed >> 8) / (double) (1u << 24) * 200.0);
            }
            const int n = std::min ({ bs, 44100 * 8 - done, 4096 - done % 4096 });
            e.process (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
            done += n;
        }
        return out;
    };

    REQUIRE (run (4096) == run (blockSize));
}
