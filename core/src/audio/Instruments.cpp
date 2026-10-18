#include "beatforge/audio/Instruments.h"

#include <algorithm>
#include <cmath>

namespace beatforge::audio
{
    namespace
    {
        const ParamSpec specs[numParams] {
            { Param::wave,      "wave",      "Wave",      "",   0.0f,     3.0f,     false },
            { Param::attack,    "attack",    "Attack",    "s",  0.001f,   2.0f,     true  },
            { Param::decay,     "decay",     "Decay",     "s",  0.01f,    3.0f,     true  },
            { Param::sustain,   "sustain",   "Sustain",   "",   0.0f,     1.0f,     false },
            { Param::release,   "release",   "Release",   "s",  0.005f,   4.0f,     true  },
            { Param::cutoff,    "cutoff",    "Cutoff",    "Hz", 40.0f,    16000.0f, true  },
            { Param::resonance, "resonance", "Resonance", "",   0.0f,     1.0f,     false },
            { Param::envAmount, "envAmount", "Env Amt",   "",   0.0f,     1.0f,     false },
            { Param::glide,     "glide",     "Glide",     "s",  0.0f,     0.5f,     false },
            { Param::level,     "level",     "Level",     "dB", -36.0f,   6.0f,     false },
        };

        //                                   wave attack  decay sustain release cutoff  res    env    glide level
        // Default levels are gain-staged so chords + bass + a full beat sum below 0 dBFS (tested).
        constexpr float polyDefaults[numParams] { 1, 0.005f, 0.3f,  0.6f, 0.25f,  2800.0f, 0.25f, 0.35f, 0.0f, -10.0f };
        constexpr float bassDefaults[numParams] { 1, 0.003f, 0.22f, 1.0f, 0.06f,  420.0f,  0.45f, 0.55f, 0.0f, -6.0f };

        float dbToGain (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

        // One-pole smoothing coefficient for a ~20 ms time constant.
        float smoothingFor (double sampleRate) noexcept { return (float) (1.0 - std::exp (-1.0 / (0.02 * sampleRate))); }

        constexpr int coefficientInterval = 8;   // filter coefficients update every 8 samples
    }

    std::string_view instrumentId (InstrumentType t) noexcept   { return t == InstrumentType::polySynth ? "polySynth" : "monoBass"; }
    std::string_view instrumentName (InstrumentType t) noexcept { return t == InstrumentType::polySynth ? "Nebula Poly" : "Nebula Bass"; }

    const ParamSpec& paramSpec (Param p) noexcept { return specs[(int) p]; }

    float defaultValue (InstrumentType t, Param p) noexcept
    {
        return (t == InstrumentType::polySynth ? polyDefaults : bassDefaults)[(int) p];
    }

    bool usesParam (InstrumentType t, Param p) noexcept
    {
        if (t == InstrumentType::polySynth)
            return p != Param::glide;

        return p == Param::wave || p == Param::decay || p == Param::cutoff || p == Param::resonance
            || p == Param::envAmount || p == Param::glide || p == Param::level;
    }

    //==============================================================================
    ParamBlock::ParamBlock (InstrumentType t)
    {
        for (int i = 0; i < numParams; ++i)
            values[(size_t) i].store (defaultValue (t, (Param) i));
    }

    void ParamBlock::set (Param p, float v) noexcept
    {
        const auto& s = paramSpec (p);
        if (p == Param::wave)
            v = std::round (v);
        values[(size_t) p].store (std::clamp (v, s.min, s.max), std::memory_order_release);
    }

    float ParamBlock::get (Param p) const noexcept  { return values[(size_t) p].load (std::memory_order_acquire); }
    dsp::Waveform ParamBlock::wave() const noexcept { return (dsp::Waveform) (int) get (Param::wave); }

    BlockParams BlockParams::from (const ParamBlock& b) noexcept
    {
        BlockParams p;
        p.wave = b.wave();
        p.attack = b.get (Param::attack);
        p.decay = b.get (Param::decay);
        p.sustain = b.get (Param::sustain);
        p.release = b.get (Param::release);
        p.cutoff = b.get (Param::cutoff);
        p.resonance = b.get (Param::resonance);
        p.envAmount = b.get (Param::envAmount);
        p.glide = b.get (Param::glide);
        p.gain = dbToGain (b.get (Param::level));
        return p;
    }

    //==============================================================================
    void PolySynthInstrument::prepare (double sr) noexcept
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        smoothing = smoothingFor (sampleRate);
        for (auto& v : voices)
        {
            v.osc.setSampleRate (sampleRate);
            v.filter.setSampleRate (sampleRate);
            v.amp.setSampleRate (sampleRate);
        }
        reset();
    }

    void PolySynthInstrument::reset() noexcept
    {
        for (auto& v : voices)
        {
            v.amp.reset();
            v.filter.reset();
            v.id = 0;
        }
        smoothedCutoff = params.cutoff;
        smoothedGain = params.gain;
    }

    void PolySynthInstrument::setParams (const BlockParams& p) noexcept
    {
        params = p;
        for (auto& v : voices)
            v.amp.setParameters (p.attack, p.decay, p.sustain, p.release);
    }

    void PolySynthInstrument::noteOn (VoiceId id, int pitch, int velocity) noexcept
    {
        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.amp.isActive()) { target = &v; break; }

        if (target == nullptr)
            for (auto& v : voices)
                if (v.amp.getStage() == dsp::Envelope::Stage::release && (target == nullptr || v.amp.getLevel() < target->amp.getLevel()))
                    target = &v;

        if (target == nullptr)
            target = &*std::min_element (voices.begin(), voices.end(),
                                         [] (const Voice& a, const Voice& b) { return a.startedAt < b.startedAt; });

        auto& v = *target;
        if (! v.amp.isActive())
        {
            v.osc.resetPhase();
            v.filter.reset();
        }
        v.id = id;
        v.pitch = pitch;
        v.velocityGain = (float) std::clamp (velocity, 1, 127) / 127.0f;
        v.osc.setFrequency (midi::pitchToFrequency (std::clamp (pitch, 0, 127)));
        v.amp.setParameters (params.attack, params.decay, params.sustain, params.release);
        v.amp.noteOn();
        v.samplesRendered = 0;
        v.startedAt = ++counter;
    }

    void PolySynthInstrument::noteOff (VoiceId id) noexcept
    {
        for (auto& v : voices)
            if (v.id == id && v.amp.isActive() && v.amp.getStage() != dsp::Envelope::Stage::release)
                v.amp.noteOff();
    }

    void PolySynthInstrument::previewOff (int pitch) noexcept
    {
        for (auto& v : voices)
            if (v.id == previewId && v.pitch == pitch)
                v.amp.noteOff();
    }

    void PolySynthInstrument::releaseAll() noexcept
    {
        for (auto& v : voices)
            v.amp.noteOff();
    }

    void PolySynthInstrument::render (float* const* channels, int numChannels, int start, int num) noexcept
    {
        // Smoothed cutoff/level are shared by all voices; advance them per sample
        // once, then render voices against the same trajectory.
        const float cutoffStart = smoothedCutoff, gainStart = smoothedGain;
        // Four-note chords plus a full drum beat stay below 0 dBFS at defaults (tested).
        constexpr float voiceHeadroom = 0.2f;

        for (auto& v : voices)
        {
            if (! v.amp.isActive())
                continue;

            float cutoff = cutoffStart, gain = gainStart;
            for (int i = start; i < start + num; ++i)
            {
                cutoff += smoothing * (params.cutoff - cutoff);
                gain += smoothing * (params.gain - gain);

                const float env = v.amp.next();
                if (v.samplesRendered++ % coefficientInterval == 0)
                    v.filter.set (cutoff * std::exp2 (4.0f * params.envAmount * env), params.resonance);

                const float s = v.filter.process (v.osc.next (params.wave)) * env * v.velocityGain * gain * voiceHeadroom;
                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] += s;

                if (! v.amp.isActive())
                    break;
            }
        }

        for (int i = 0; i < num; ++i)
        {
            smoothedCutoff += smoothing * (params.cutoff - smoothedCutoff);
            smoothedGain += smoothing * (params.gain - smoothedGain);
        }
    }

    int PolySynthInstrument::activeVoices() const noexcept
    {
        return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.amp.isActive(); });
    }

    //==============================================================================
    void MonoBassInstrument::prepare (double sr) noexcept
    {
        sampleRate = sr > 0.0 ? sr : 48000.0;
        smoothing = smoothingFor (sampleRate);
        osc.setSampleRate (sampleRate);
        filter.setSampleRate (sampleRate);
        amp.setSampleRate (sampleRate);
        filterEnv.setSampleRate (sampleRate);
        reset();
    }

    void MonoBassInstrument::reset() noexcept
    {
        heldCount = 0;
        amp.reset();
        filterEnv.reset();
        filter.reset();
        soundingPitch = -1;
        glideSamplesLeft = 0;
        smoothedCutoff = params.cutoff;
        smoothedGain = params.gain;
    }

    void MonoBassInstrument::setParams (const BlockParams& p) noexcept
    {
        params = p;
        amp.setParameters (0.003f, 0.01f, 1.0f, 0.06f);          // gate-like amp
        filterEnv.setParameters (0.001f, p.decay, 0.0f, p.decay); // pluck that decays to the base cutoff
    }

    void MonoBassInstrument::startPitch (int pitch, int velocity, bool legato) noexcept
    {
        targetFrequency = midi::pitchToFrequency (std::clamp (pitch, 0, 127));
        velocityGain = (float) std::clamp (velocity, 1, 127) / 127.0f;
        soundingPitch = pitch;

        if (legato && params.glide > 0.0f)
        {
            glideSamplesLeft = std::max (1, (int) (params.glide * (float) sampleRate));
            glideRatioPerSample = std::pow (targetFrequency / frequency, 1.0 / glideSamplesLeft);
            return;   // legato: no retrigger
        }

        glideSamplesLeft = 0;
        frequency = targetFrequency;

        if (! legato)
        {
            if (! amp.isActive())
            {
                osc.resetPhase();
                filter.reset();
            }
            amp.noteOn();
            filterEnv.noteOn();
        }
    }

    void MonoBassInstrument::noteOn (VoiceId id, int pitch, int velocity) noexcept
    {
        const bool legato = heldCount > 0 && amp.isActive() && amp.getStage() != dsp::Envelope::Stage::release;

        if (heldCount == (int) held.size())   // full: forget the oldest
        {
            std::move (held.begin() + 1, held.end(), held.begin());
            --heldCount;
        }
        held[(size_t) heldCount++] = { id, pitch, velocity };
        startPitch (pitch, velocity, legato);
    }

    void MonoBassInstrument::followHeldNotes() noexcept
    {
        if (heldCount == 0)
        {
            amp.noteOff();
            filterEnv.noteOff();
            soundingPitch = -1;
            return;
        }

        const auto& top = held[(size_t) heldCount - 1];
        if (top.pitch != soundingPitch)
            startPitch (top.pitch, top.velocity, amp.isActive() && amp.getStage() != dsp::Envelope::Stage::release);
    }

    void MonoBassInstrument::noteOff (VoiceId id) noexcept
    {
        int kept = 0;
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].id != id)
                held[(size_t) kept++] = held[(size_t) i];

        if (kept != heldCount)
        {
            heldCount = kept;
            followHeldNotes();
        }
    }

    void MonoBassInstrument::previewOff (int pitch) noexcept
    {
        int kept = 0;
        for (int i = 0; i < heldCount; ++i)
            if (! (held[(size_t) i].id == PolySynthInstrument::previewId && held[(size_t) i].pitch == pitch))
                held[(size_t) kept++] = held[(size_t) i];

        if (kept != heldCount)
        {
            heldCount = kept;
            followHeldNotes();
        }
    }

    void MonoBassInstrument::releaseAll() noexcept
    {
        heldCount = 0;
        followHeldNotes();
    }

    void MonoBassInstrument::render (float* const* channels, int numChannels, int start, int num) noexcept
    {
        // Leaves room for kick + bass together (the resonant pluck peaks high).
        constexpr float bassHeadroom = 0.4f;

        for (int i = start; i < start + num; ++i)
        {
            smoothedCutoff += smoothing * (params.cutoff - smoothedCutoff);
            smoothedGain += smoothing * (params.gain - smoothedGain);

            if (! amp.isActive())
                continue;

            if (glideSamplesLeft > 0)
            {
                frequency *= glideRatioPerSample;
                if (--glideSamplesLeft == 0)
                    frequency = targetFrequency;
            }
            osc.setFrequency (frequency);

            const float a = amp.next();
            const float f = filterEnv.next();
            if (samplesRendered++ % coefficientInterval == 0)
                filter.set (smoothedCutoff * std::exp2 (5.0f * params.envAmount * f), params.resonance);

            const float s = filter.process (osc.next (params.wave)) * a * velocityGain * smoothedGain * bassHeadroom;
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] += s;
        }
    }

    //==============================================================================
    InstrumentRack::InstrumentRack() = default;

    void InstrumentRack::prepare (double sampleRate) noexcept
    {
        beginBlock();
        poly.prepare (sampleRate);
        bass.prepare (sampleRate);
    }

    void InstrumentRack::reset() noexcept
    {
        poly.reset();
        bass.reset();
    }

    void InstrumentRack::beginBlock() noexcept
    {
        const auto requested = getType();
        if (requested != active)
        {
            // Notes of the old instrument fade out; new notes go to the new one.
            if (active == InstrumentType::polySynth) poly.releaseAll(); else bass.releaseAll();
            active = requested;
        }

        poly.setParams (BlockParams::from (polyParams));
        bass.setParams (BlockParams::from (bassParams));
    }
}
