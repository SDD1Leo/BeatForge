#pragma once

#include "beatforge/audio/Instruments.h"

#include <array>
#include <span>
#include <string_view>

namespace beatforge::audio
{
    // Factory sounds: complete parameter sets for one instrument. Levels stay
    // at or below the instrument's default, so chords + bass + a full beat keep
    // their headroom whatever presets are chosen (tested).
    struct InstrumentPreset
    {
        std::string_view id;      // "pluck"
        std::string_view name;    // "Pluck"
        InstrumentType type;
        std::array<float, numParams> values;   // indexed by Param
    };

    std::span<const InstrumentPreset> presetsFor (InstrumentType) noexcept;
    const InstrumentPreset* findPreset (InstrumentType, std::string_view id) noexcept;
    // The preset whose values the given parameters equal, or nullptr ("custom").
    const InstrumentPreset* matchingPreset (InstrumentType, const std::array<float, numParams>& values) noexcept;
}
