#include "AllocationGuard.h"

#include <beatforge/audio/AudioEngine.h>
#include <beatforge/dsp/Envelope.h>
#include <beatforge/dsp/Oscillator.h>
#include <beatforge/dsp/StateVariableFilter.h>
#include <beatforge/project/ProjectFile.h>
#include <beatforge/project/Session.h>

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace beatforge;
using audio::InstrumentType;
using audio::Param;
using Catch::Approx;

namespace
{
    constexpr double sr = 48000.0;

    float peak (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
    {
        float p = 0.0f;
        for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, std::abs (x[i]));
        return p;
    }

    double rms (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (to - from));
    }

    // RMS of the first difference: a proxy for high-frequency content.
    double brightness (const std::vector<float>& x, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from + 1; i < to; ++i) s += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        return std::sqrt (s / (double) (to - from)) / std::max (1e-12, rms (x, from, to));
    }

    int risingCrossings (const std::vector<float>& x, size_t from, size_t to)
    {
        int n = 0;
        for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f) ++n;
        return n;
    }

    template <typename Instrument>
    std::vector<float> renderInstrument (Instrument& inst, int numSamples)
    {
        std::vector<float> out ((size_t) numSamples, 0.0f);
        float* ch[] { out.data() };
        inst.render (ch, 1, 0, numSamples);
        return out;
    }

    audio::BlockParams paramsFor (InstrumentType t, std::initializer_list<std::pair<Param, float>> changes = {})
    {
        audio::ParamBlock block (t);
        for (auto [p, v] : changes) block.set (p, v);
        return audio::BlockParams::from (block);
    }

    std::vector<float> renderEngine (audio::AudioEngine& e, int numSamples, int block = 512)
    {
        std::vector<float> l ((size_t) block), r ((size_t) block), out;
        float* ch[] { l.data(), r.data() };
        for (int done = 0; done < numSamples; done += block)
        {
            const int n = std::min (block, numSamples - done);
            e.process (ch, 2, n);
            out.insert (out.end(), l.begin(), l.begin() + n);
        }
        return out;
    }
}

//==============================================================================
TEST_CASE ("Every waveform plays its frequency within range", "[dsp][oscillator]")
{
    const auto wave = GENERATE (dsp::Waveform::sine, dsp::Waveform::saw, dsp::Waveform::square, dsp::Waveform::triangle);
    const double freq = GENERATE (110.0, 440.0, 1000.0);
    CAPTURE ((int) wave, freq);

    dsp::Oscillator osc;
    osc.setSampleRate (sr);
    osc.setFrequency (freq);
    std::vector<float> x (48000);
    for (auto& s : x) s = osc.next (wave);

    REQUIRE (std::abs (risingCrossings (x, 4800, 48000) - freq * 0.9) <= 2.0);
    REQUIRE (peak (x, 4800) < 1.2f);
    REQUIRE (peak (x, 4800) > 0.7f);
}

TEST_CASE ("Low-pass passes lows, cuts highs, resonates, and stays stable", "[dsp][filter]")
{
    auto gainAt = [] (double freq, double cutoff, double res)
    {
        dsp::StateVariableLowPass f;
        f.setSampleRate (sr);
        f.set (cutoff, res);
        float out = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            const float y = f.process ((float) std::sin (2.0 * std::numbers::pi * freq * i / sr));
            if (i > 24000) out = std::max (out, std::abs (y));
        }
        return 20.0 * std::log10 (out);
    };

    REQUIRE (gainAt (200.0, 2000.0, 0.0) == Approx (0.0).margin (0.5));
    REQUIRE (gainAt (16000.0, 1000.0, 0.0) < -40.0);
    REQUIRE (gainAt (1000.0, 1000.0, 0.9) > 15.0);
    REQUIRE (gainAt (1000.0, 1000.0, 0.9) == Approx (20.0 * std::log10 (dsp::StateVariableLowPass::magnitude (1000.0, 1000.0, 0.9, sr))).margin (1.0));
    REQUIRE (gainAt (4000.0, 1000.0, 0.0) == Approx (20.0 * std::log10 (dsp::StateVariableLowPass::magnitude (4000.0, 1000.0, 0.0, sr))).margin (1.0));

    // Sweep the cutoff every sample at full resonance: no blow-up.
    dsp::StateVariableLowPass f;
    f.setSampleRate (sr);
    float worst = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        f.set (40.0 * std::pow (400.0, 0.5 + 0.5 * std::sin (i * 0.003)), 1.0);
        const float y = f.process (i % 100 < 50 ? 1.0f : -1.0f);
        REQUIRE (std::isfinite (y));
        worst = std::max (worst, std::abs (y));
    }
    REQUIRE (worst < 60.0f);
}

TEST_CASE ("ADSR timing is sample-accurate", "[dsp][envelope]")
{
    dsp::Envelope env;
    env.setSampleRate (1000.0);   // 1 sample = 1 ms
    env.setParameters (0.010f, 0.020f, 0.5f, 0.040f);
    env.noteOn();

    std::vector<float> x;
    for (int i = 0; i < 100; ++i) x.push_back (env.next());
    REQUIRE (x[9] == Approx (1.0f));                 // peak after 10 samples
    REQUIRE (x[29] == Approx (0.5f));                // sustain after a further 20
    REQUIRE (env.getStage() == dsp::Envelope::Stage::sustain);

    env.noteOff();
    int samples = 0;
    while (env.isActive() && samples < 1000)
    {
        env.setParameters (0.010f, 0.020f, 0.5f, 2.0f);   // changing params mid-release must not stretch it
        env.next();
        ++samples;
    }
    REQUIRE (samples == 40);
}

TEST_CASE ("Retriggering continues from the current level", "[dsp][envelope]")
{
    dsp::Envelope env;
    env.setSampleRate (1000.0);
    env.setParameters (0.010f, 0.010f, 0.8f, 0.100f);
    env.noteOn();
    for (int i = 0; i < 30; ++i) env.next();
    env.noteOff();
    for (int i = 0; i < 50; ++i) env.next();
    const float before = env.getLevel();
    env.noteOn();
    REQUIRE (env.next() == Approx (before + 0.1f).margin (0.001f));   // no jump to 0
}

//==============================================================================
TEST_CASE ("Poly synth controls change the actual sound", "[instrument][poly]")
{
    auto play = [] (std::initializer_list<std::pair<Param, float>> changes)
    {
        audio::PolySynthInstrument synth;
        synth.prepare (sr);
        synth.setParams (paramsFor (InstrumentType::polySynth, changes));
        synth.noteOn (1, 48, 127);
        return renderInstrument (synth, 24000);
    };

    const auto base = play ({ { Param::envAmount, 0.0f } });
    const auto dark = play ({ { Param::envAmount, 0.0f }, { Param::cutoff, 300.0f } });
    const auto quiet = play ({ { Param::envAmount, 0.0f }, { Param::level, audio::defaultValue (InstrumentType::polySynth, Param::level) - 6.0f } });
    const auto square = play ({ { Param::envAmount, 0.0f }, { Param::wave, 2.0f } });
    const auto slow = play ({ { Param::envAmount, 0.0f }, { Param::attack, 0.3f } });

    REQUIRE (brightness (dark, 4800, 24000) < brightness (base, 4800, 24000) * 0.5);
    REQUIRE (peak (quiet, 4800) == Approx (peak (base, 4800) * 0.5f).epsilon (0.05));   // 6 dB below the default
    REQUIRE (base != square);
    REQUIRE (peak (slow, 0, 2400) < peak (base, 0, 2400) * 0.3f);                        // 50 ms in: still rising
}

TEST_CASE ("Filter envelope opens the filter at the note start", "[instrument][poly]")
{
    audio::PolySynthInstrument synth;
    synth.prepare (sr);
    synth.setParams (paramsFor (InstrumentType::polySynth, { { Param::cutoff, 300.0f }, { Param::envAmount, 1.0f },
                                                             { Param::decay, 0.2f }, { Param::sustain, 0.0f }, { Param::release, 0.05f } }));
    synth.noteOn (1, 48, 127);
    const auto x = renderInstrument (synth, 24000);
    REQUIRE (brightness (x, 0, 2400) > brightness (x, 12000, 14400) * 1.5);   // bright, then darker as it decays
}

TEST_CASE ("Bass is monophonic with last-note priority", "[instrument][bass]")
{
    audio::MonoBassInstrument bass;
    bass.prepare (sr);
    bass.setParams (paramsFor (InstrumentType::monoBass));

    bass.noteOn (1, 36, 100);   // C2
    bass.noteOn (2, 43, 100);   // G2 (legato, no glide: jumps)
    renderInstrument (bass, 480);
    REQUIRE (bass.activeVoices() == 1);
    REQUIRE (bass.currentFrequency() == Approx (midi::pitchToFrequency (43)));

    bass.noteOff (2);           // back to the held C2
    renderInstrument (bass, 480);
    REQUIRE (bass.currentFrequency() == Approx (midi::pitchToFrequency (36)));

    bass.noteOff (1);
    const auto tail = renderInstrument (bass, 24000);
    REQUIRE (peak (tail, 12000) < 1e-4f);
    REQUIRE (bass.activeVoices() == 0);
}

TEST_CASE ("Bass glides between overlapping notes", "[instrument][bass]")
{
    audio::MonoBassInstrument bass;
    bass.prepare (sr);
    bass.setParams (paramsFor (InstrumentType::monoBass, { { Param::glide, 0.1f } }));

    bass.noteOn (1, 36, 100);
    renderInstrument (bass, 2400);
    bass.noteOn (2, 48, 100);   // an octave up, legato
    renderInstrument (bass, 2400);   // halfway through the 100 ms glide
    REQUIRE (bass.currentFrequency() == Approx (std::sqrt (midi::pitchToFrequency (36) * midi::pitchToFrequency (48))).epsilon (0.01));
    renderInstrument (bass, 4800);
    REQUIRE (bass.currentFrequency() == Approx (midi::pitchToFrequency (48)));

    // A note after silence does not glide.
    bass.noteOff (1);
    bass.noteOff (2);
    renderInstrument (bass, 24000);
    bass.noteOn (3, 40, 100);
    renderInstrument (bass, 1);
    REQUIRE (bass.currentFrequency() == Approx (midi::pitchToFrequency (40)));
}

TEST_CASE ("Switching instruments releases the old one's notes", "[instrument][engine]")
{
    audio::AudioEngine engine;
    engine.prepare (sr);
    midi::Clip clip { 3840, { { 1, 48, 100, 0, 3840 } } };
    engine.setMidiClip (&clip);
    engine.play();
    renderEngine (engine, 4800);
    REQUIRE (engine.getActiveSynthVoices() == 1);

    engine.getInstruments().setType (InstrumentType::monoBass);
    renderEngine (engine, 24000);
    REQUIRE (engine.getActiveSynthVoices() == 0);   // poly note released; bass waits for the next note-on

    renderEngine (engine, 96000);                     // the clip loops: the bass plays it now
    REQUIRE (engine.getInstruments().getBass().activeVoices() == 1);
}

TEST_CASE ("Parameter changes and instrument switches are allocation-free", "[instrument][engine][realtime]")
{
    audio::AudioEngine engine;
    engine.prepare (sr);
    midi::Clip clip { 3840, { { 1, 40, 127, 0, 3840 } } };
    engine.setMidiClip (&clip);
    auto& params = engine.getInstruments().params (InstrumentType::polySynth);
    params.set (Param::envAmount, 0.0f);
    engine.play();
    renderEngine (engine, 4800);

    std::vector<float> l (512), r (512);
    float* ch[] { l.data(), r.data() };
    beatforge::test::ScopedAllocationCounter allocations;
    params.set (Param::cutoff, 8000.0f);
    for (int i = 0; i < 40; ++i)
        engine.process (ch, 2, 512);
    params.set (Param::cutoff, 200.0f);
    engine.getInstruments().setType (InstrumentType::monoBass);
    engine.getInstruments().setType (InstrumentType::polySynth);
    for (int i = 0; i < 40; ++i)
        engine.process (ch, 2, 512);
    REQUIRE (allocations.count() == 0);
}

TEST_CASE ("Cutoff set on the engine changes what it plays", "[instrument][engine]")
{
    auto brightnessAt = [] (float cutoff)
    {
        audio::AudioEngine engine;
        engine.prepare (sr);
        midi::Clip clip { 3840, { { 1, 40, 127, 0, 3840 } } };
        engine.setMidiClip (&clip);
        auto& params = engine.getInstruments().params (InstrumentType::polySynth);
        params.set (Param::envAmount, 0.0f);
        params.set (Param::cutoff, cutoff);
        engine.play();
        const auto out = renderEngine (engine, 24000);
        return brightness (out, 4800, 24000);
    };
    REQUIRE (brightnessAt (200.0f) < brightnessAt (8000.0f) * 0.5);
}

//==============================================================================
TEST_CASE ("Instrument choice and settings go through the session", "[session][instrument]")
{
    audio::AudioEngine engine;
    project::Session session (engine);
    int instrumentChanges = 0;
    session.addListener ([&] (project::Session::Change c) { instrumentChanges += c == project::Session::Change::instrument; });

    session.setInstrument (InstrumentType::monoBass);
    REQUIRE (engine.getInstruments().getType() == InstrumentType::monoBass);
    REQUIRE (session.isDirty());

    session.setInstrumentParam (InstrumentType::monoBass, Param::cutoff, 99999.0f);
    REQUIRE (session.getInstrumentParam (InstrumentType::monoBass, Param::cutoff) == 16000.0f);
    REQUIRE (engine.getInstruments().params (InstrumentType::monoBass).get (Param::cutoff) == 16000.0f);

    session.setInstrumentParam (InstrumentType::polySynth, Param::wave, 2.4f);
    REQUIRE (session.getInstrumentParam (InstrumentType::polySynth, Param::wave) == 2.0f);

    session.setInstrumentParam (InstrumentType::polySynth, Param::wave, 2.0f);   // no-op
    REQUIRE (instrumentChanges == 3);

    session.resetInstrumentParams (InstrumentType::monoBass);
    REQUIRE (session.getInstrumentParam (InstrumentType::monoBass, Param::cutoff)
             == audio::defaultValue (InstrumentType::monoBass, Param::cutoff));
}

TEST_CASE ("Instrument and settings survive save and load", "[session][instrument][file]")
{
    audio::AudioEngine engine;
    project::Session session (engine);
    session.setInstrument (InstrumentType::monoBass);
    session.setInstrumentParam (InstrumentType::monoBass, Param::wave, 2.0f);
    session.setInstrumentParam (InstrumentType::monoBass, Param::cutoff, 650.0f);
    session.setInstrumentParam (InstrumentType::monoBass, Param::glide, 0.08f);
    session.setInstrumentParam (InstrumentType::polySynth, Param::attack, 0.4f);

    const auto text = project::toJson (session.getProject(), "test");
    const auto j = nlohmann::json::parse (text);
    REQUIRE (j["tracks"][1]["instrument"] == "monoBass");
    REQUIRE (j["tracks"][1]["params"]["monoBass"]["wave"] == "square");
    REQUIRE (j["tracks"][1]["params"]["monoBass"]["cutoff"] == 650.0);

    const auto loaded = project::fromJson (text);
    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.empty());
    REQUIRE (*loaded.project == session.getProject());

    audio::AudioEngine engine2;
    project::Session reopened (engine2);
    reopened.openProject (*loaded.project, std::nullopt);
    REQUIRE (engine2.getInstruments().getType() == InstrumentType::monoBass);
    REQUIRE (engine2.getInstruments().params (InstrumentType::monoBass).get (Param::glide) == Approx (0.08f));
    REQUIRE (engine2.getInstruments().params (InstrumentType::polySynth).get (Param::attack) == Approx (0.4f));
}

TEST_CASE ("Older and odd instrument data loads safely", "[instrument][file][compat]")
{
    SECTION ("0.5 basicSynth becomes Nebula Poly with defaults")
    {
        const auto r = project::fromJson (R"({"format":"beatforge-project","version":1,"name":"Old","tempo":120,
            "timeSignature":{"numerator":4,"denominator":4},"tracks":[{"type":"drumMachine","name":"Drums","lanes":[]},
            {"type":"instrument","instrument":"basicSynth","name":"Synth","clip":{"lengthTicks":3840,"notes":[]}}]})");
        REQUIRE (r.ok());
        REQUIRE (r.warnings.empty());
        REQUIRE (r.project->instrument == InstrumentType::polySynth);
        REQUIRE (r.project->instrumentParams == project::Project::defaultInstrumentParams());
        REQUIRE (r.project->synthClip.has_value());
    }

    SECTION ("bad values are corrected, unknown settings kept")
    {
        const auto r = project::fromJson (R"({"format":"beatforge-project","version":1,"name":"T","tempo":120,
            "timeSignature":{"numerator":4,"denominator":4},"tracks":[
            {"type":"instrument","instrument":"monoBass","name":"Bass","params":{
                "monoBass":{"cutoff":-5,"wave":"noise","resonance":0.3,"drive":0.7},
                "fmSynth":{"ratio":2}}}]})");
        REQUIRE (r.ok());
        REQUIRE (r.project->instrument == InstrumentType::monoBass);
        REQUIRE (r.project->instrumentParams[1][(size_t) Param::cutoff] == 40.0f);
        REQUIRE (r.project->instrumentParams[1][(size_t) Param::resonance] == Approx (0.3f));
        REQUIRE (r.project->instrumentParams[1][(size_t) Param::wave] == audio::defaultValue (InstrumentType::monoBass, Param::wave));
        REQUIRE (std::any_of (r.warnings.begin(), r.warnings.end(),
                              [] (const std::string& w) { return w.find ("2 Nebula Bass setting(s)") != std::string::npos; }));

        const auto resaved = nlohmann::json::parse (project::toJson (*r.project, "test"));
        const auto& synth = resaved["tracks"][1];
        REQUIRE (synth["params"]["monoBass"]["drive"] == 0.7);
        REQUIRE (synth["params"]["fmSynth"]["ratio"] == 2);
        REQUIRE_FALSE (synth.contains ("otherInstrumentParams"));
        REQUIRE_FALSE (synth.contains ("clip"));
    }
}

TEST_CASE ("Default levels leave headroom for chords and a full beat", "[instrument][engine][levels]")
{
    for (const auto type : { InstrumentType::polySynth, InstrumentType::monoBass })
    {
        CAPTURE (std::string (audio::instrumentName (type)));
        audio::AudioEngine engine;
        engine.prepare (sr);
        engine.getInstruments().setType (type);

        // Four-note chords on every beat (the bass plays the newest note).
        midi::Clip clip { 3840, {} };
        midi::NoteId id = 1;
        for (int beat = 0; beat < 4; ++beat)
            for (int pitch : { 36, 43, 48, 52 })
                clip.notes.push_back ({ id++, pitch + (type == InstrumentType::polySynth ? 12 : 0), 127, (time::Tick) beat * 960, 900 });
        engine.setMidiClip (&clip);

        auto& drums = engine.getDrumMachine();
        for (int s = 0; s < 16; ++s)
            for (int lane = 0; lane < audio::DrumMachine::numLanes; ++lane)
                drums.setStep (lane, s, lane == 0 ? s % 4 == 0 : lane == 2 || s % 4 == 2);

        engine.play();
        const auto out = renderEngine (engine, 96000 * 2);
        CAPTURE (20.0 * std::log10 (peak (out)));
        REQUIRE (peak (out) < 1.0f);
    }
}
