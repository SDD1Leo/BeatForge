#pragma once

#include "beatforge/midi/Note.h"

#include <array>
#include <cstdint>

namespace beatforge::audio
{
    // Basic polyphonic synth used to hear MIDI clips (Phase 5): band-limited
    // saw softened by a one-pole low-pass, linear ADSR, velocity-scaled.
    // Real-time safe: fixed voices, no allocation. Voices are keyed by note id
    // so overlapping notes of the same pitch release correctly.
    class PolySynth
    {
    public:
        static constexpr int numVoices = 16;
        static constexpr midi::NoteId previewId = 0xffffffffu;   // auditions from the editor

        void prepare (double sampleRate) noexcept;
        void reset() noexcept;   // silence immediately

        void noteOn (midi::NoteId, int pitch, int velocity) noexcept;
        void noteOff (midi::NoteId) noexcept;
        void previewOff (int pitch) noexcept;
        void releaseAll() noexcept;

        // Releases every sounding voice for which `keep (id)` is false.
        template <typename Keep>
        void releaseUnless (Keep&& keep) noexcept
        {
            for (auto& v : voices)
                if (v.stage != Stage::idle && v.stage != Stage::release && v.id != previewId && ! keep (v.id))
                    v.stage = Stage::release;
        }

        // Adds into channels[ch][start .. start + num).
        void render (float* const* channels, int numChannels, int start, int num) noexcept;

        int getActiveVoiceCount() const noexcept;
        bool isSounding() const noexcept { return getActiveVoiceCount() > 0; }

        static constexpr float attackSeconds = 0.004f, decaySeconds = 0.25f, sustainLevel = 0.55f,
                               releaseSeconds = 0.12f, voiceGain = 0.22f;

    private:
        enum class Stage { idle, attack, decay, sustain, release };

        struct Voice
        {
            midi::NoteId id = 0;
            int pitch = 0;
            float velocityGain = 0.0f;
            double phase = 0.0, increment = 0.0;
            float level = 0.0f, lowpass = 0.0f, lowpassCoeff = 0.0f;
            Stage stage = Stage::idle;
            std::uint64_t startedAt = 0;
        };

        float nextSample (Voice&) noexcept;

        std::array<Voice, numVoices> voices {};
        double sampleRate = 48000.0;
        float attackStep = 0.0f, decayStep = 0.0f, releaseStep = 0.0f;
        std::uint64_t noteCounter = 0;
    };
}
