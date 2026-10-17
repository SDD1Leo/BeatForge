#pragma once

#include <string_view>

namespace beatforge
{
    // Version of the BeatForge application, taken from the CMake project version.
    std::string_view versionString() noexcept;

    // Version of the JUCE release the application was built against.
    std::string_view juceVersionString() noexcept;
}
