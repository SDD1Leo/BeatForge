#pragma once

#include "beatforge/dsp/Envelope.h"
#include "beatforge/dsp/Oscillator.h"
#include "beatforge/dsp/StateVariableFilter.h"
#include "beatforge/midi/Note.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace beatforge::audio
{
    // Identifies a sounding note (clip + note), so note-offs find their voice.
    using VoiceId = std::uint64_t;

    enum class InstrumentType { polySynth, monoBass };
    constexpr int numInstrumentTypes = 2;

    std::string_view instrumentId (InstrumentType) noexcept;     // "polySynth", "monoBass"
    std::string_view instrumentName (InstrumentType) noexcept;   // "Nebula Poly", "Nebula Bass"

    // Every instrument parameter, with its range and default per instrument.
    enum class Param { wave, attack, decay, sustain, release, cutoff, resonance, envAmount, glide, level };
    constexpr int numParams = 10;

    struct ParamSpec
    {
        Param param;
        std::string_view id;      // file key, e.g. "cutoff"
        std::string_view label;   // UI label, e.g. "Cutoff"
        std::string_view unit;    // "s", "Hz", "dB", "" ...
        float min, max;
        bool logarithmic;         // knob travel feels even in octaves / time
    };

    const ParamSpec& paramSpec (Param) noexcept;
    float defaultValue (InstrumentType, Param) noexcept;
    bool usesParam (InstrumentType, Param) noexcept;   // which knobs an instrument shows

    // Parameter values shared between the UI/session (writer) and the audio
    // thread (reader). Lock-free; the wave is stored as its index.
    class ParamBlock
    {
    public:
        explicit ParamBlock (InstrumentType);
        void set (Param, float) noexcept;   // clamped to the spec range
        float get (Param) const noexcept;
        dsp::Waveform wave() const noexcept;

    private:
        std::array<std::atomic<float>, numParams> values;
    };

    //==============================================================================
    // Values an instrument reads once per block (smoothed where audible).
    struct BlockParams
    {
        dsp::Waveform wave = dsp::Waveform::saw;
        float attack = 0.005f, decay = 0.3f, sustain = 0.6f, release = 0.25f;
        float cutoff = 2800.0f, resonance = 0.25f, envAmount = 0.4f, glide = 0.0f, gain = 0.5f;

        static BlockParams from (const ParamBlock&) noexcept;
    };

    // Polyphonic subtractive synth: oscillator -> resonant low-pass (opened by
    // the envelope) -> amp ADSR. 16 voices keyed by note id, oldest stolen.
    class PolySynthInstrument
    {
    public:
        static constexpr int numVoices = 16;

        void prepare (double sampleRate) noexcept;
        void reset() noexcept;
        void setParams (const BlockParams&) noexcept;

        void noteOn (VoiceId, int pitch, int velocity) noexcept;
        void noteOff (VoiceId) noexcept;
        void previewOff (int pitch) noexcept;
        void releaseAll() noexcept;
        template <typename Keep> void releaseUnless (Keep&& keep) noexcept
        {
            for (auto& v : voices)
                if (v.amp.isActive() && v.amp.getStage() != dsp::Envelope::Stage::release && v.id != previewId && ! keep (v.id))
                    v.amp.noteOff();
        }

        void render (float* const* channels, int numChannels, int start, int num) noexcept;
        int activeVoices() const noexcept;

        static constexpr VoiceId previewId = ~VoiceId (0);

    private:
        struct Voice
        {
            VoiceId id = 0;
            int pitch = 0;
            float velocityGain = 0.0f;
            dsp::Oscillator osc;
            dsp::StateVariableLowPass filter;
            dsp::Envelope amp;
            std::uint64_t startedAt = 0;
            std::uint32_t samplesRendered = 0;   // paces filter updates independently of block size
        };

        std::array<Voice, numVoices> voices;
        BlockParams params;
        double sampleRate = 48000.0;
        float smoothedCutoff = 2800.0f, smoothedGain = 0.5f, smoothing = 0.001f;
        std::uint64_t counter = 0;
    };

    // Monophonic bass: one voice, last-note priority, legato glide, filter
    // envelope that decays to zero (classic plucky bass).
    class MonoBassInstrument
    {
    public:
        void prepare (double sampleRate) noexcept;
        void reset() noexcept;
        void setParams (const BlockParams&) noexcept;

        void noteOn (VoiceId, int pitch, int velocity) noexcept;
        void noteOff (VoiceId) noexcept;
        void previewOff (int pitch) noexcept;
        void releaseAll() noexcept;
        template <typename Keep> void releaseUnless (Keep&& keep) noexcept
        {
            int kept = 0;
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i].id == PolySynthInstrument::previewId || keep (held[(size_t) i].id))
                    held[(size_t) kept++] = held[(size_t) i];
            heldCount = kept;
            followHeldNotes();
        }

        void render (float* const* channels, int numChannels, int start, int num) noexcept;
        int activeVoices() const noexcept { return amp.isActive() ? 1 : 0; }
        double currentFrequency() const noexcept { return frequency; }

    private:
        struct Held { VoiceId id; int pitch; int velocity; };

        void followHeldNotes() noexcept;   // sound the newest held note, or release
        void startPitch (int pitch, int velocity, bool legato) noexcept;

        std::array<Held, 16> held {};
        int heldCount = 0;

        dsp::Oscillator osc;
        dsp::StateVariableLowPass filter;
        dsp::Envelope amp, filterEnv;
        BlockParams params;
        double sampleRate = 48000.0;
        double frequency = 110.0, targetFrequency = 110.0, glideRatioPerSample = 1.0;
        int glideSamplesLeft = 0, soundingPitch = -1;
        std::uint32_t samplesRendered = 0;
        float velocityGain = 0.0f;
        float smoothedCutoff = 400.0f, smoothedGain = 0.5f, smoothing = 0.001f;
    };

    // The synth track's instrument: both instruments exist; the selected one
    // plays. Switching releases the previous one's notes. Audio-thread object
    // except for the ParamBlocks and the type, which are atomics.
    class InstrumentRack
    {
    public:
        InstrumentRack();

        void prepare (double sampleRate) noexcept;
        void reset() noexcept;

        // Any thread.
        void setType (InstrumentType t) noexcept { requestedType.store ((int) t, std::memory_order_release); }
        InstrumentType getType() const noexcept   { return (InstrumentType) requestedType.load (std::memory_order_acquire); }
        ParamBlock& params (InstrumentType t) noexcept { return t == InstrumentType::polySynth ? polyParams : bassParams; }

        // Audio thread.
        void beginBlock() noexcept;
        void noteOn (VoiceId id, int pitch, int velocity) noexcept { if (active == InstrumentType::polySynth) poly.noteOn (id, pitch, velocity); else bass.noteOn (id, pitch, velocity); }
        void noteOff (VoiceId id) noexcept     { poly.noteOff (id); bass.noteOff (id); }
        void previewOff (int pitch) noexcept        { poly.previewOff (pitch); bass.previewOff (pitch); }
        void releaseAll() noexcept                  { poly.releaseAll(); bass.releaseAll(); }
        template <typename Keep> void releaseUnless (Keep&& keep) noexcept { poly.releaseUnless (keep); bass.releaseUnless (keep); }
        void render (float* const* channels, int numChannels, int start, int num) noexcept
        {
            poly.render (channels, numChannels, start, num);   // a released instrument rings out
            bass.render (channels, numChannels, start, num);
        }
        int activeVoices() const noexcept { return poly.activeVoices() + bass.activeVoices(); }

        const MonoBassInstrument& getBass() const noexcept { return bass; }

    private:
        PolySynthInstrument poly;
        MonoBassInstrument bass;
        ParamBlock polyParams { InstrumentType::polySynth }, bassParams { InstrumentType::monoBass };
        std::atomic<int> requestedType { (int) InstrumentType::polySynth };
        InstrumentType active = InstrumentType::polySynth;
    };
}
