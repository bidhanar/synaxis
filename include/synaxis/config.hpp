// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include "synaxis/catalog.hpp"

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace synaxis {

// Where the app keeps its files, following the XDG base directory spec.
struct AppPaths {
    std::filesystem::path config_file;  // $XDG_CONFIG_HOME/synaxis/config.json
    std::filesystem::path data_dir;     // $XDG_DATA_HOME/synaxis
};

AppPaths DefaultAppPaths();

struct AppConfig {
    std::vector<std::string> roots;  // folders to scan
    double min_size_mb = 200;        // skip smaller files (samples, clips)
    int port = 8484;
    bool fullscreen = true;          // open VLC fullscreen
    std::string vlc = "vlc";         // VLC executable

    ScanOptions scan_options() const;
    bool operator==(const AppConfig&) const = default;
};

// Defaults: scan the home directory.
AppConfig DefaultConfig();
// Missing file or fields fall back to the defaults.
AppConfig LoadConfig(const std::filesystem::path& path);
void SaveConfig(const AppConfig& config, const std::filesystem::path& path);

nlohmann::json ToJson(const AppConfig& config);
// Applies the fields present in `j` on top of `base`. Throws
// nlohmann::json::exception on wrongly-typed fields.
AppConfig ApplyJson(AppConfig base, const nlohmann::json& j);

} // namespace synaxis
