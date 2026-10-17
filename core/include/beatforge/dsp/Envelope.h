#pragma once

#include <algorithm>

namespace beatforge::dsp
{
    // Linear ADSR, sample-accurate. Times in seconds, sustain 0..1.
    // Retriggering starts the attack from the current level (no click).
    // setParameters() may be called every block: a release in progress keeps
    // the slope it started with; new times apply to the next stage/note.
    class Envelope
    {
    public:
        enum class Stage { idle, attack, decay, sustain, release };

        void setSampleRate (double sr) noexcept { sampleRate = sr > 0.0 ? sr : 48000.0; }

        void setParameters (float attackSeconds, float decaySeconds, float sustainLevel, float releaseSeconds) noexcept
        {
            sustain = std::clamp (sustainLevel, 0.0f, 1.0f);
            attackStep = step (attackSeconds, 1.0f);
            decayStep = step (decaySeconds, 1.0f - sustain);
            releaseTime = releaseSeconds;
        }

        void noteOn() noexcept   { stage = Stage::attack; }
        void noteOff() noexcept
        {
            if (stage != Stage::idle)
            {
                stage = Stage::release;
                releaseStep = step (releaseTime, std::max (level, 1e-6f));
            }
        }
        void reset() noexcept    { stage = Stage::idle; level = 0.0f; }

        float next() noexcept
        {
            switch (stage)
            {
                // Each stage ends within half a step of its target, so float
                // rounding cannot add an extra sample to its length.
                case Stage::attack:
                    level += attackStep;
                    if (level >= 1.0f - 0.5f * attackStep) { level = 1.0f; stage = Stage::decay; }
                    break;
                case Stage::decay:
                    level -= decayStep;
                    if (level <= sustain + 0.5f * decayStep) { level = sustain; stage = Stage::sustain; }
                    break;
                case Stage::sustain:
                    level = sustain;   // follows sustain changes while held
                    break;
                case Stage::release:
                    level -= releaseStep;
                    if (level <= 0.5f * releaseStep) { level = 0.0f; stage = Stage::idle; }
                    break;
                case Stage::idle:
                    break;
            }
            return level;
        }

        Stage getStage() const noexcept { return stage; }
        bool isActive() const noexcept  { return stage != Stage::idle; }
        float getLevel() const noexcept { return level; }

    private:
        float step (float seconds, float distance) const noexcept
        {
            const float samples = std::max (1.0f, seconds * (float) sampleRate);
            return std::max (distance, 1e-6f) / samples;
        }

        double sampleRate = 48000.0;
        Stage stage = Stage::idle;
        float level = 0.0f, sustain = 1.0f;
        float attackStep = 1.0f, decayStep = 1.0f, releaseStep = 1.0f, releaseTime = 0.1f;
    };
}
