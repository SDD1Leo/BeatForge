#include "AllocationGuard.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/midi/NoteSchedule.h>

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
    const int releaseSamples = (int) (audio::PolySynth::releaseSeconds * 48000.0) + 2;
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

TEST_CASE ("Schedule orders events and wraps note-offs at the clip end", "[midi][schedule]")
{
    const auto s = midi::NoteSchedule::build (clipWith ({ { 0, 60, 90, 480, 480 },   // off at 960
                                                          { 0, 64, 80, 0, 3840 },    // off at clip end -> 0
                                                          { 0, 67, 70, 960, 240 },   // on at 960 (after the off)
                                                          { 0, 72, 70, 4000, 240 } })); // beyond the clip: skipped
    REQUIRE (s->notes.size() == 3);
    REQUIRE (s->events.size() == 6);
    REQUIRE ((s->events[0].tick == 0 && ! s->events[0].on && s->events[0].pitch == 64));   // wrapped off first
    REQUIRE ((s->events[1].tick == 0 && s->events[1].on && s->events[1].pitch == 64));
    REQUIRE ((s->events[2].tick == 480 && s->events[2].on));
    REQUIRE ((s->events[3].tick == 960 && ! s->events[3].on && s->events[3].pitch == 60));
    REQUIRE ((s->events[4].tick == 960 && s->events[4].on && s->events[4].pitch == 67));
    REQUIRE (s->covers (2, 3839.0));
    REQUIRE_FALSE (s->covers (1, 960.0));
    REQUIRE_FALSE (s->covers (99, 0.0));
}

TEST_CASE ("Schedule exchange hands over without losing or leaking", "[midi][schedule][realtime]")
{
    midi::ScheduleExchange exchange;
    bool changed = false;
    REQUIRE (exchange.acquire (changed) == nullptr);
    REQUIRE_FALSE (changed);

    exchange.publish (midi::NoteSchedule::build (clipWith ({ { 0, 60, 100, 0, 240 } })));
    const auto* first = exchange.acquire (changed);
    REQUIRE (changed);
    REQUIRE (first->notes.size() == 1);

    // The first schedule had no predecessor, so the retired slot is free and
    // the next one is taken over immediately.
    exchange.publish (midi::NoteSchedule::build (clipWith ({ { 0, 60, 100, 0, 240 }, { 0, 62, 100, 240, 240 } })));
    const auto* second = exchange.acquire (changed);
    REQUIRE (changed);
    REQUIRE (second->notes.size() == 2);

    // publish() collects what the audio thread retired, so every newer schedule
    // is taken over at the next block; the audio side never frees memory.
    exchange.publish (midi::NoteSchedule::build (clipWith ({})));
    REQUIRE (exchange.acquire (changed)->notes.empty());
    REQUIRE (changed);
    REQUIRE (exchange.acquire (changed)->notes.empty());
    REQUIRE_FALSE (changed);

    // Two publishes before the audio thread looks: only the newest is played,
    // the skipped one is freed by the message thread.
    exchange.publish (midi::NoteSchedule::build (clipWith ({ { 0, 60, 100, 0, 240 } })));
    exchange.publish (midi::NoteSchedule::build (clipWith ({ { 0, 60, 100, 0, 240 }, { 0, 61, 100, 0, 240 }, { 0, 62, 100, 0, 240 } })));
    REQUIRE (exchange.acquire (changed)->notes.size() == 3);
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
    audio::PolySynth synth;
    synth.prepare (48000.0);
    synth.noteOn (1, 69, 127);

    std::vector<float> out (48000, 0.0f);
    float* ch[] { out.data() };
    synth.render (ch, 1, 0, 48000);

    int crossings = 0;
    for (size_t i = 24001; i < 48000; ++i)
        if (out[i - 1] < 0.0f && out[i] >= 0.0f) ++crossings;
    REQUIRE (std::abs (crossings - 220) <= 1);   // 440 Hz over 0.5 s

    REQUIRE (peak (out, 0, 48000) <= audio::PolySynth::voiceGain * 1.1f);
    REQUIRE (peak (out, 0, 48000) > audio::PolySynth::voiceGain * 0.5f);
}

TEST_CASE ("Velocity scales the level; release fades to silence", "[midi][synth]")
{
    auto level = [] (int velocity)
    {
        audio::PolySynth synth;
        synth.prepare (48000.0);
        synth.noteOn (1, 60, velocity);
        std::vector<float> out (9600, 0.0f);
        float* ch[] { out.data() };
        synth.render (ch, 1, 0, 9600);
        return peak (out, 0, 9600);
    };
    REQUIRE (level (64) == Catch::Approx (level (127) * 64.0f / 127.0f).epsilon (0.01));

    audio::PolySynth synth;
    synth.prepare (48000.0);
    synth.noteOn (1, 60, 100);
    std::vector<float> out (48000, 0.0f);
    float* ch[] { out.data() };
    synth.render (ch, 1, 0, 4800);
    synth.noteOff (1);
    synth.render (ch, 1, 4800, 43200);
    REQUIRE (peak (out, 4800 + (size_t) releaseSamples, 48000) == 0.0f);
    REQUIRE_FALSE (synth.isSounding());
}

TEST_CASE ("Overlapping notes of the same pitch release by id", "[midi][synth]")
{
    audio::PolySynth synth;
    synth.prepare (48000.0);
    synth.noteOn (1, 60, 100);
    synth.noteOn (2, 60, 100);
    REQUIRE (synth.getActiveVoiceCount() == 2);
    synth.noteOff (1);

    std::vector<float> out (48000, 0.0f);
    float* ch[] { out.data() };
    synth.render (ch, 1, 0, 48000);
    REQUIRE (synth.getActiveVoiceCount() == 1);   // note 2 still holds
}

TEST_CASE ("Voice stealing keeps polyphony bounded", "[midi][synth]")
{
    audio::PolySynth synth;
    synth.prepare (48000.0);
    for (int i = 0; i < 40; ++i)
        synth.noteOn ((midi::NoteId) i + 1, 40 + i, 100);
    REQUIRE (synth.getActiveVoiceCount() == audio::PolySynth::numVoices);
}

//==============================================================================
TEST_CASE ("A note starts and stops on its exact samples", "[midi][engine]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 69, 100, 960, 480 } });   // beat 2, an eighth long
    engine.setMidiClip (&clip);
    engine.play();
    const auto out = render (engine, 96000);

    // Silent until the note-on at 24000; the first rendered sample of a saw from
    // phase 0 is exactly 0, the next is not.
    REQUIRE (firstNonZero (out) == 24000 + 1);

    // Same note but longer: identical until the note-off at 36000, different after.
    audio::AudioEngine longer;
    longer.prepare (48000.0);
    const auto longClip = clipWith ({ { 0, 69, 100, 960, 960 } });
    longer.setMidiClip (&longClip);
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
        e.setMidiClip (&clip);
        e.getDrumMachine().setStep (0, 0, true);
        e.getDrumMachine().setStep (0, 8, true);
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
    engine.setMidiClip (&clip);
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
    engine.setMidiClip (&clip);
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
        engine.setMidiClip (&clip);
        engine.play();
        render (engine, 24000);   // half a bar in: sounding
        REQUIRE (engine.getActiveSynthVoices() == 1);

        const auto edited = moveInsteadOfDelete ? clipWith ({ { 0, 60, 100, 3000, 240 } }) : clipWith ({});
        engine.setMidiClip (&edited);
        const auto out = render (engine, 24000);
        REQUIRE (peak (out, (size_t) releaseSamples + 512, 24000) == 0.0f);
        REQUIRE (engine.getActiveSynthVoices() == 0);
    }
}

TEST_CASE ("Stop releases every note", "[midi][engine][stuck]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 60, 100, 0, 3840 }, { 0, 64, 100, 0, 3840 }, { 0, 67, 100, 0, 3840 } });
    engine.setMidiClip (&clip);
    engine.play();
    render (engine, 12000);
    REQUIRE (engine.getActiveSynthVoices() == 3);
    engine.stop();
    const auto out = render (engine, 24000);
    REQUIRE (peak (out, (size_t) releaseSamples + 512, 24000) == 0.0f);
    REQUIRE (engine.getActiveSynthVoices() == 0);
}

TEST_CASE ("A loop wrap releases notes that cross the loop end", "[midi][engine][stuck]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    const auto clip = clipWith ({ { 0, 60, 100, 2880, 1920 } }, 3840 * 2);   // beat 4 of bar 1 into bar 2
    engine.setMidiClip (&clip);
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
    engine.previewNoteOn (60, 100);
    auto held = render (engine, 9600);
    REQUIRE (peak (held, 0, 9600) > 0.05f);
    engine.previewNoteOff (60);
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
    engine.setMidiClip (&first);
    engine.play();

    for (int round = 0; round < 3; ++round)
    {
        {
            beatforge::test::ScopedAllocationCounter allocations;
            engine.previewNoteOn (72, 90);
            for (int i = 0; i < 200; ++i)
                engine.process (chans, 2, 256);
            engine.previewNoteOff (72);
            REQUIRE (allocations.count() == 0);
        }
        // Message thread work (allocates by design): new clip, garbage collection.
        engine.setMidiClip (round % 2 == 0 ? &second : &first);
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
        e.setMidiClip (&clip);
        for (int s = 0; s < 16; s += 3) e.getDrumMachine().setStep (2, s, true);
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
