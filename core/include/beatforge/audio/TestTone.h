#pragma once

namespace beatforge::audio
{
    // Sine test tone with a short linear gain ramp so starting and stopping
    // never clicks. Real-time safe: no allocation, no locks.
    class TestTone
    {
    public:
        static constexpr double defaultFrequencyHz = 440.0;
        static constexpr float defaultGainDb = -18.0f;
        static constexpr double rampSeconds = 0.010;

        void prepare (double sampleRate) noexcept;

        // Ramp towards audible (true) or silent (false).
        void setActive (bool shouldBeActive) noexcept;

        // True while any non-zero output is still possible (active or fading out).
        bool isSounding() const noexcept;

        // Adds nothing: writes the tone into channels [0, numChannels), replacing
        // their contents. Every channel receives the same signal.
        void render (float* const* channels, int numChannels, int numSamples) noexcept;

        double getSampleRate() const noexcept { return sampleRate; }

    private:
        double sampleRate = 48000.0;
        double phase = 0.0;            // [0, 1)
        double phaseIncrement = 0.0;
        float peakGain = 0.0f;
        float currentGain = 0.0f;      // ramp position in [0, 1]
        float rampStep = 0.0f;
        bool active = false;
    };
}
