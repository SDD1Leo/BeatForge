#include "beatforge/audio/InstrumentPresets.h"

#include <cmath>

namespace beatforge::audio
{
    namespace
    {
        constexpr auto poly = InstrumentType::polySynth;
        constexpr auto bass = InstrumentType::monoBass;

        //                                          wave attack decay  sustain release cutoff   res    env    glide level
        constexpr InstrumentPreset polyPresets[] {
            { "init",        "Init",        poly, { 1, 0.005f, 0.3f,  0.6f,  0.25f,  2800.0f, 0.25f, 0.35f, 0.0f, -10.0f } },
            { "pluck",       "Pluck",       poly, { 1, 0.002f, 0.25f, 0.0f,  0.15f,  1800.0f, 0.3f,  0.6f,  0.0f, -10.0f } },
            { "soft_pad",    "Soft Pad",    poly, { 1, 0.6f,   1.0f,  0.8f,  1.2f,   1400.0f, 0.15f, 0.2f,  0.0f, -14.0f } },
            { "bright_lead", "Bright Lead", poly, { 2, 0.005f, 0.2f,  0.8f,  0.12f,  5000.0f, 0.35f, 0.3f,  0.0f, -13.0f } },
            { "keys",        "Keys",        poly, { 3, 0.003f, 0.8f,  0.3f,  0.3f,   3200.0f, 0.1f,  0.25f, 0.0f, -10.0f } },
            { "warm_sine",   "Warm Sine",   poly, { 0, 0.01f,  0.3f,  0.7f,  0.3f,   8000.0f, 0.0f,  0.0f,  0.0f, -10.0f } },
        };

        // Bass: attack, sustain and release are fixed by the instrument (filter pluck); values kept at defaults.
        constexpr InstrumentPreset bassPresets[] {
            { "init",        "Init",        bass, { 1, 0.003f, 0.22f, 1.0f,  0.06f,  420.0f,  0.45f, 0.55f, 0.0f,  -6.0f } },
            { "sub",         "Sub",         bass, { 0, 0.003f, 0.3f,  1.0f,  0.06f,  300.0f,  0.0f,  0.1f,  0.0f,  -6.0f } },
            { "acid",        "Acid",        bass, { 1, 0.003f, 0.18f, 1.0f,  0.06f,  300.0f,  0.8f,  0.85f, 0.06f, -8.0f } },
            { "punchy",      "Punchy",      bass, { 2, 0.003f, 0.12f, 1.0f,  0.06f,  520.0f,  0.3f,  0.7f,  0.0f,  -7.0f } },
            { "smooth_saw",  "Smooth Saw",  bass, { 1, 0.003f, 0.6f,  1.0f,  0.06f,  900.0f,  0.2f,  0.3f,  0.04f, -8.0f } },
        };
    }

    std::span<const InstrumentPreset> presetsFor (InstrumentType t) noexcept
    {
        if (t == poly) return polyPresets;
        return bassPresets;
    }

    const InstrumentPreset* findPreset (InstrumentType t, std::string_view id) noexcept
    {
        for (auto& p : presetsFor (t))
            if (p.id == id)
                return &p;
        return nullptr;
    }

    const InstrumentPreset* matchingPreset (InstrumentType t, const std::array<float, numParams>& values) noexcept
    {
        for (auto& p : presetsFor (t))
        {
            bool same = true;
            for (int i = 0; i < numParams && same; ++i)
                same = ! usesParam (t, (Param) i) || std::abs (p.values[(size_t) i] - values[(size_t) i]) < 1e-5f;
            if (same)
                return &p;
        }
        return nullptr;
    }
}
