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

#include "synaxis/filename_parser.hpp"
#include "synaxis/media_probe.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace synaxis {

// A video file on disk plus everything known about it.
struct VideoFile {
    std::string id;                 // FileId(path): stable across runs
    std::filesystem::path path;     // canonical
    std::uintmax_t size = 0;        // bytes
    std::int64_t mtime = 0;         // unix seconds; doubles as "date added"
    ParsedFilename metadata;
    bool probed = false;            // ProbeMedia has run (even if it failed)
    std::optional<MediaInfo> info;  // set when probing succeeded

    bool operator==(const VideoFile&) const = default;
};

// Short hex hash of a path, used as the file's id in URLs and saved state.
std::string FileId(const std::filesystem::path& path);

struct ScanOptions {
    std::vector<std::filesystem::path> roots;
    std::uintmax_t min_size = 0;  // bytes; smaller files are skipped
};

// Walks every root for video files of at least `min_size` bytes. Hidden
// directories are skipped, as are symlinked directories (loops); unreadable
// directories are ignored rather than aborting the scan. Overlapping roots
// don't produce duplicates. Results are sorted by path, with metadata parsed
// but not probed. Returns early (partial results) once `cancel` is set.
std::vector<VideoFile> ScanVideoFiles(const ScanOptions& options,
                                      const std::atomic<bool>* cancel = nullptr);

// Caches scan + probe results so startup doesn't re-probe every file.
// Metadata isn't stored: it's re-parsed on load so parser improvements apply.
void SaveVideoFiles(const std::vector<VideoFile>& files, const std::filesystem::path& path);
// Returns an empty list if the file is missing or unreadable.
std::vector<VideoFile> LoadVideoFiles(const std::filesystem::path& path);

enum class TitleKind { kMovie, kShow };

// One playable thing within a title: a movie, or one episode of a show.
// When the library holds several copies (different rips or resolutions),
// they're all listed, best first.
struct TitleItem {
    std::optional<int> season;
    std::optional<int> episode;
    std::vector<std::string> file_ids;
};

// What the user browses: a movie, or a show with its episodes.
struct Title {
    std::string id;
    TitleKind kind = TitleKind::kMovie;
    std::string name;
    std::optional<int> year;
    std::vector<TitleItem> items;  // movies: exactly one; shows: in watch order
    std::int64_t added = 0;        // newest file's mtime
};

// An immutable view of the library: files grouped into titles. Episodes
// are grouped by series name, movies by name and year.
class Catalog {
public:
    Catalog() = default;
    explicit Catalog(std::vector<VideoFile> files);

    const std::vector<VideoFile>& files() const { return files_; }
    const std::vector<Title>& titles() const { return titles_; }

    const VideoFile* FindFile(const std::string& id) const;
    const Title* FindTitle(const std::string& id) const;
    // The title holding `file_id` and the index of its item.
    std::optional<std::pair<const Title*, size_t>> Locate(const std::string& file_id) const;

private:
    std::vector<VideoFile> files_;
    std::vector<Title> titles_;
    std::unordered_map<std::string, size_t> file_index_;
    std::unordered_map<std::string, size_t> title_index_;
    std::unordered_map<std::string, std::pair<size_t, size_t>> file_location_;
};

} // namespace synaxis
