#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace beatforge
{
    // Paths <-> UTF-8 text. On Windows, path::string() and path(std::string) use
    // the ANSI code page, which garbles (or throws on) names outside it; these
    // keep every character on every platform.
    inline std::filesystem::path pathFromUtf8 (std::string_view s)
    {
        return std::filesystem::path (std::u8string (s.begin(), s.end()));
    }

    inline std::string pathToUtf8 (const std::filesystem::path& p)
    {
        const auto u = p.u8string();
        return std::string (u.begin(), u.end());
    }
}
