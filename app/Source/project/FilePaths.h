#pragma once

#include <beatforge/util/Paths.h>

#include <juce_core/juce_core.h>

namespace beatforge
{
    // juce::File <-> std::filesystem::path without losing characters (UTF-8 on
    // every platform; see beatforge/util/Paths.h).
    inline std::filesystem::path toPath (const juce::File& f) { return pathFromUtf8 (f.getFullPathName().toStdString()); }
    inline juce::File toFile (const std::filesystem::path& p) { return juce::File (juce::String::fromUTF8 (pathToUtf8 (p).c_str())); }
}
