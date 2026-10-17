#include "beatforge/audio/PolySynth.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beatforge::audio
{
    namespace
    {
        // PolyBLEP correction for a naive saw (reduces aliasing).
        double polyBlep (double t, double dt) noexcept
        {
            if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
            if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
            return 0.0;
        }
    }

    void PolySynth::prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        attackStep = (float) (1.0 / (attackSeconds * sampleRate));
        decayStep = (float) ((1.0 - sustainLevel) / (decaySeconds * sampleRate));
        releaseStep = (float) (1.0 / (releaseSeconds * sampleRate));
        reset();
    }

    void PolySynth::reset() noexcept
    {
        for (auto& v : voices)
            v = {};
    }

    void PolySynth::noteOn (midi::NoteId id, int pitch, int velocity) noexcept
    {
        // Free voice, else the quietest releasing one, else the oldest.
        Voice* target = nullptr;
        for (auto& v : voices)
            if (v.stage == Stage::idle) { target = &v; break; }

        if (target == nullptr)
            for (auto& v : voices)
                if (v.stage == Stage::release && (target == nullptr || v.level < target->level))
                    target = &v;

        if (target == nullptr)
            target = &*std::min_element (voices.begin(), voices.end(),
                                         [] (const Voice& a, const Voice& b) { return a.startedAt < b.startedAt; });

        const double freq = midi::pitchToFrequency (std::clamp (pitch, midi::minPitch, midi::maxPitch));
        auto& v = *target;
        v.id = id;
        v.pitch = pitch;
        v.velocityGain = voiceGain * (float) std::clamp (velocity, 1, 127) / 127.0f;
        v.phase = 0.0;
        v.increment = freq / sampleRate;
        v.level = 0.0f;   // stolen voices restart from 0: the attack ramp avoids a click
        v.lowpass = 0.0f;
        // Cutoff ~6x the fundamental (bright but not buzzy), capped below Nyquist.
        const double cutoff = std::min (freq * 6.0, sampleRate * 0.45);
        v.lowpassCoeff = (float) (1.0 - std::exp (-2.0 * std::numbers::pi * cutoff / sampleRate));
        v.stage = Stage::attack;
        v.startedAt = ++noteCounter;
    }

    void PolySynth::noteOff (midi::NoteId id) noexcept
    {
        for (auto& v : voices)
            if (v.id == id && v.stage != Stage::idle && v.stage != Stage::release)
                v.stage = Stage::release;
    }

    void PolySynth::previewOff (int pitch) noexcept
    {
        for (auto& v : voices)
            if (v.id == previewId && v.pitch == pitch && v.stage != Stage::idle)
                v.stage = Stage::release;
    }

    void PolySynth::releaseAll() noexcept
    {
        for (auto& v : voices)
            if (v.stage != Stage::idle)
                v.stage = Stage::release;
    }

    float PolySynth::nextSample (Voice& v) noexcept
    {
        switch (v.stage)
        {
            case Stage::attack:
                v.level += attackStep;
                if (v.level >= 1.0f) { v.level = 1.0f; v.stage = Stage::decay; }
                break;
            case Stage::decay:
                v.level -= decayStep;
                if (v.level <= sustainLevel) { v.level = sustainLevel; v.stage = Stage::sustain; }
                break;
            case Stage::sustain:
                break;
            case Stage::release:
                v.level -= releaseStep;
                if (v.level <= 0.0f) { v.level = 0.0f; v.stage = Stage::idle; return 0.0f; }
                break;
            case Stage::idle:
                return 0.0f;
        }

        const double saw = 2.0 * v.phase - 1.0 - polyBlep (v.phase, v.increment);
        v.phase += v.increment;
        v.phase -= std::floor (v.phase);

        v.lowpass += v.lowpassCoeff * ((float) saw - v.lowpass);
        return v.lowpass * v.level * v.velocityGain;
    }

    void PolySynth::render (float* const* channels, int numChannels, int start, int num) noexcept
    {
        for (auto& v : voices)
        {
            if (v.stage == Stage::idle)
                continue;

            for (int i = start; i < start + num; ++i)
            {
                const float s = nextSample (v);
                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] += s;

                if (v.stage == Stage::idle)
                    break;
            }
        }
    }

    int PolySynth::getActiveVoiceCount() const noexcept
    {
        return (int) std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.stage != Stage::idle; });
    }
}
