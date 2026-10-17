#pragma once

namespace beatforge::audio
{
    // Click voice: short decaying sine, higher and louder on the downbeat.
    // Monophonic - a new click restarts the voice. Real-time safe.
    class Metronome
    {
    public:
        static constexpr double accentHz = 1500.0, normalHz = 1000.0;
        static constexpr float accentGain = 0.5f, normalGain = 0.3f;
        static constexpr double clickSeconds = 0.030;

        void prepare (double sampleRate) noexcept;
        void reset() noexcept { remaining = 0; }

        void trigger (bool accent) noexcept;
        bool isSounding() const noexcept { return remaining > 0; }

        // Adds the click into channels[ch][start .. start + num).
        void render (float* const* channels, int numChannels, int start, int num) noexcept;

    private:
        double sampleRate = 48000.0;
        double phase = 0.0, phaseIncrement = 0.0;
        float gain = 0.0f, envelope = 0.0f, decayPerSample = 0.0f;
        int remaining = 0, fadeLength = 1, length = 0;
    };
}
