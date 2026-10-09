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

#include "synaxis/media_library.hpp"

#include <algorithm>
#include <fstream>
#include <regex>

#include <nlohmann/json.hpp>

namespace synaxis {

namespace {

using nlohmann::json;

// Cheap pre-filter so the (relatively expensive) FilenameParser regexes only
// run on files that are actually video — skips scanning past subtitles,
// artwork, .nfo files, etc. in large libraries. `extensions` is always
// either DefaultVideoExtensions() or a caller-validated subset of it (see
// MediaLibrary::DefaultVideoExtensions()), so it's safe to splice directly
// into the pattern without escaping.
std::regex BuildVideoExtensionRegex(const std::vector<std::string>& extensions) {
    std::string pattern = R"(\.()";
    for (size_t i = 0; i < extensions.size(); ++i) {
        if (i) pattern += '|';
        pattern += extensions[i];
    }
    pattern += R"()$)";
    return std::regex(pattern, std::regex::icase);
}

json ToJson(const MediaEntry& entry) {
    const ParsedFilename& m = entry.metadata;
    return json{
        {"path", entry.path.string()},
        {"season", m.season ? json(*m.season) : json(nullptr)},
        {"episode", m.episode ? json(*m.episode) : json(nullptr)},
        {"year", m.year ? json(*m.year) : json(nullptr)},
        {"source", m.source ? json(*m.source) : json(nullptr)},
        {"title", m.title ? json(*m.title) : json(nullptr)},
        {"tags", m.tags},
        {"episode_title", m.episode_title ? json(*m.episode_title) : json(nullptr)},
    };
}

MediaEntry FromJson(const json& j) {
    MediaEntry entry;
    entry.path = j.at("path").get<std::string>();
    if (!j.at("season").is_null()) entry.metadata.season = j.at("season").get<int>();
    if (!j.at("episode").is_null()) entry.metadata.episode = j.at("episode").get<int>();
    if (!j.at("year").is_null()) entry.metadata.year = j.at("year").get<int>();
    if (!j.at("source").is_null()) entry.metadata.source = j.at("source").get<std::string>();
    if (!j.at("title").is_null()) entry.metadata.title = j.at("title").get<std::string>();
    entry.metadata.tags = j.at("tags").get<std::vector<std::string>>();
    // Absent in libraries written before episode titles were parsed.
    if (j.contains("episode_title") && !j.at("episode_title").is_null()) {
        entry.metadata.episode_title = j.at("episode_title").get<std::string>();
    }
    return entry;
}

} // namespace

const std::vector<std::string>& MediaLibrary::DefaultVideoExtensions() {
    static const std::vector<std::string> kDefaultVideoExtensions = {
        "mp4", "mkv", "mov", "avi", "webm", "wmv", "flv", "m4v", "mpg", "mpeg", "ts",
    };
    return kDefaultVideoExtensions;
}

std::vector<MediaEntry> MediaLibrary::Scan(const std::filesystem::path& root,
                                            const std::vector<std::string>& extensions) {
    const std::regex video_extension =
        BuildVideoExtensionRegex(extensions.empty() ? DefaultVideoExtensions() : extensions);

    std::vector<MediaEntry> entries;
    for (const auto& dir_entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!dir_entry.is_regular_file()) continue;
        if (!std::regex_search(dir_entry.path().filename().string(), video_extension)) continue;

        MediaEntry entry;
        entry.path = std::filesystem::canonical(dir_entry.path());
        entry.metadata = FilenameParser::parse(dir_entry.path().filename().string());
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const MediaEntry& a, const MediaEntry& b) { return a.path < b.path; });
    return entries;
}

void MediaLibrary::SaveToJson(const std::vector<MediaEntry>& entries,
                               const std::filesystem::path& path) {
    json arr = json::array();
    for (const auto& entry : entries) arr.push_back(ToJson(entry));

    std::ofstream out(path);
    out << json{{"entries", arr}}.dump(2);
}

std::vector<MediaEntry> MediaLibrary::LoadFromJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    json root;
    in >> root;

    std::vector<MediaEntry> entries;
    for (const auto& j : root.at("entries")) entries.push_back(FromJson(j));
    return entries;
}

} // namespace synaxis
