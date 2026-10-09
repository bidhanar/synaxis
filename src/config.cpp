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

#include "synaxis/config.hpp"

#include <cstdlib>
#include <fstream>

namespace synaxis {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path Home() {
    const char* home = std::getenv("HOME");
    return home && *home ? fs::path(home) : fs::current_path();
}

fs::path XdgDir(const char* var, const char* fallback) {
    const char* dir = std::getenv(var);
    return dir && *dir ? fs::path(dir) : Home() / fallback;
}

} // namespace

AppPaths DefaultAppPaths() {
    return {XdgDir("XDG_CONFIG_HOME", ".config") / "synaxis" / "config.json",
            XdgDir("XDG_DATA_HOME", ".local/share") / "synaxis"};
}

ScanOptions AppConfig::scan_options() const {
    ScanOptions options;
    for (const std::string& root : roots) options.roots.emplace_back(root);
    options.min_size = std::uintmax_t(std::max(0.0, min_size_mb) * 1024 * 1024);
    return options;
}

AppConfig DefaultConfig() {
    AppConfig config;
    config.roots = {Home().string()};
    return config;
}

json ToJson(const AppConfig& c) {
    return {{"roots", c.roots},
            {"min_size_mb", c.min_size_mb},
            {"port", c.port},
            {"fullscreen", c.fullscreen},
            {"vlc", c.vlc}};
}

AppConfig ApplyJson(AppConfig c, const json& j) {
    if (j.contains("roots")) c.roots = j.at("roots").get<std::vector<std::string>>();
    if (j.contains("min_size_mb")) c.min_size_mb = j.at("min_size_mb").get<double>();
    if (j.contains("port")) c.port = j.at("port").get<int>();
    if (j.contains("fullscreen")) c.fullscreen = j.at("fullscreen").get<bool>();
    if (j.contains("vlc")) c.vlc = j.at("vlc").get<std::string>();
    return c;
}

AppConfig LoadConfig(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return DefaultConfig();
    json j = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) return DefaultConfig();
    try {
        return ApplyJson(DefaultConfig(), j);
    } catch (const json::exception&) {
        return DefaultConfig();
    }
}

void SaveConfig(const AppConfig& config, const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    out << ToJson(config).dump(2) << "\n";
}

} // namespace synaxis
