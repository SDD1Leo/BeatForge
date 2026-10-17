#include "beatforge/audio/DrumKit.h"

#include "beatforge/dsp/Biquad.h"
#include "beatforge/dsp/Noise.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace beatforge::audio
{
    namespace
    {
        constexpr double twoPi = 2.0 * std::numbers::pi;

        std::vector<float> makeBuffer (double sampleRate, double seconds)
        {
            return std::vector<float> ((size_t) std::max (1.0, seconds * sampleRate), 0.0f);
        }

        double decay (double t, double timeConstant) { return std::exp (-t / timeConstant); }

        // Peak levels leave headroom for every drum landing on the same step:
        // all five together stay below 0 dBFS at default lane volumes.
        constexpr float kickPeak = 0.44f;    // -7 dBFS
        constexpr float snarePeak = 0.32f;   // -10 dBFS
        constexpr float hatPeak = 0.18f;     // -15 dBFS
        constexpr float clapPeak = 0.28f;    // -11 dBFS

        // Scales to the target peak and fades the last 3 ms so every sample ends at 0.
        void finish (std::vector<float>& s, double sampleRate, float peakTarget)
        {
            float peak = 0.0f;
            for (auto v : s) peak = std::max (peak, std::abs (v));
            const float scale = peak > 0.0f ? peakTarget / peak : 0.0f;

            const auto fade = std::min (s.size(), (size_t) (0.003 * sampleRate));
            for (size_t i = 0; i < s.size(); ++i)
            {
                const auto fromEnd = s.size() - 1 - i;
                const float ramp = fromEnd < fade ? (float) fromEnd / (float) fade : 1.0f;
                s[i] *= scale * ramp;
            }
        }

        std::vector<float> kick (double sr)
        {
            auto s = makeBuffer (sr, 0.55);
            double phase = 0.0;
            dsp::Noise noise (1);

            for (size_t i = 0; i < s.size(); ++i)
            {
                const double t = (double) i / sr;
                const double freq = 48.0 + 110.0 * decay (t, 0.035);   // 158 Hz sweeping to 48 Hz
                phase += freq / sr;
                const double body = std::sin (twoPi * phase) * decay (t, 0.28);
                const double click = noise.next() * decay (t, 0.0015) * 0.35;
                s[i] = (float) std::tanh (1.6 * (body + click));
            }

            finish (s, sr, kickPeak);
            return s;
        }

        std::vector<float> snare (double sr)
        {
            auto s = makeBuffer (sr, 0.32);
            dsp::Noise noise (2);
            dsp::Biquad hp;
            hp.setup (dsp::Biquad::Type::highPass, sr, 1800.0, 0.7);
            double p1 = 0.0, p2 = 0.0;

            for (size_t i = 0; i < s.size(); ++i)
            {
                const double t = (double) i / sr;
                p1 += 185.0 / sr;
                p2 += 330.0 / sr;
                const double tone = (0.6 * std::sin (twoPi * p1) + 0.4 * std::sin (twoPi * p2)) * decay (t, 0.045);
                const double rattle = hp.process (noise.next()) * decay (t, 0.075);
                s[i] = (float) (0.55 * tone + 0.8 * rattle);
            }

            finish (s, sr, snarePeak);
            return s;
        }

        // 808-style metallic source: six detuned square waves, band-limited by filters.
        std::vector<float> hat (double sr, double seconds, double timeConstant, std::uint32_t seed)
        {
            auto s = makeBuffer (sr, seconds);
            const double freqs[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            double phases[6] {};
            dsp::Noise noise (seed);
            dsp::Biquad bp, hp;
            bp.setup (dsp::Biquad::Type::bandPass, sr, 9000.0, 0.8);
            hp.setup (dsp::Biquad::Type::highPass, sr, 6500.0, 0.7);

            for (size_t i = 0; i < s.size(); ++i)
            {
                const double t = (double) i / sr;
                double metal = 0.0;
                for (int k = 0; k < 6; ++k)
                {
                    phases[k] += freqs[k] * 1.7 / sr;
                    phases[k] -= std::floor (phases[k]);
                    metal += phases[k] < 0.5 ? 1.0 : -1.0;
                }
                const double source = metal / 6.0 + 0.5 * noise.next();
                const double attack = std::min (1.0, t / 0.0008);
                s[i] = (float) (hp.process (bp.process ((float) source)) * decay (t, timeConstant) * attack);
            }

            finish (s, sr, hatPeak);
            return s;
        }

        std::vector<float> clap (double sr)
        {
            auto s = makeBuffer (sr, 0.35);
            dsp::Noise noise (5);
            dsp::Biquad bp;
            bp.setup (dsp::Biquad::Type::bandPass, sr, 1150.0, 1.4);
            const double bursts[] { 0.0, 0.011, 0.022 };

            for (size_t i = 0; i < s.size(); ++i)
            {
                const double t = (double) i / sr;
                double env = 0.0;
                for (auto b : bursts)
                    if (t >= b && t < b + 0.011)
                        env = std::max (env, decay (t - b, 0.004));
                if (t >= 0.033)
                    env = std::max (env, 0.75 * decay (t - 0.033, 0.07));

                s[i] = (float) (bp.process (noise.next()) * env);
            }

            finish (s, sr, clapPeak);
            return s;
        }
    }

    std::string_view drumSoundName (DrumSound s) noexcept
    {
        switch (s)
        {
            case DrumSound::kick:      return "Kick";
            case DrumSound::snare:     return "Snare";
            case DrumSound::closedHat: return "Closed Hat";
            case DrumSound::openHat:   return "Open Hat";
            case DrumSound::clap:      return "Clap";
        }
        return "?";
    }

    DrumKit DrumKit::generate (double sampleRate)
    {
        DrumKit kit;
        kit.samples[(size_t) DrumSound::kick] = kick (sampleRate);
        kit.samples[(size_t) DrumSound::snare] = snare (sampleRate);
        kit.samples[(size_t) DrumSound::closedHat] = hat (sampleRate, 0.12, 0.022, 3);
        kit.samples[(size_t) DrumSound::openHat] = hat (sampleRate, 0.65, 0.16, 4);
        kit.samples[(size_t) DrumSound::clap] = clap (sampleRate);
        return kit;
    }
}
