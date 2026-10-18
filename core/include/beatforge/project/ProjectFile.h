#pragma once

#include "beatforge/project/Project.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace beatforge::project
{
    // On-disk format: UTF-8 JSON, extension ".beatforge".
    //   { "format": "beatforge-project", "version": 2, ... }
    // Readers accept any version <= currentVersion; newer files are refused
    // (never half-read). Version 1 (one looping pattern + one looping clip,
    // 0.4-0.6) is upgraded in memory to the arrangement model.
    constexpr const char* fileExtension = ".beatforge";
    constexpr const char* formatName = "beatforge-project";
    constexpr int currentVersion = 2;

    struct LoadResult
    {
        std::optional<Project> project;      // empty on error
        std::string error;                   // why loading failed
        std::vector<std::string> warnings;   // what was corrected while loading
        int fileVersion = 0;                 // version found in the file

        bool ok() const noexcept { return project.has_value(); }
    };

    struct SaveResult
    {
        std::string error;   // empty on success
        bool ok() const noexcept { return error.empty(); }
    };

    std::string toJson (const Project&, const std::string& appVersion);
    LoadResult fromJson (const std::string& text);

    LoadResult loadFile (const std::filesystem::path&);

    // Writes "<file>.tmp" then renames it over the target, so a failed save
    // never damages an existing project file.
    SaveResult saveFile (const Project&, const std::filesystem::path&, const std::string& appVersion);
}
