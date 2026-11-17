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

// Tells whether two people have the same video file without uploading it,
// for Watch Together. A file's fingerprint is its size plus a SHA-256 of its
// first and last 4 MB; its length in seconds rides along so a different copy
// of the same film can be recognised too. The format is Tandem's
// (local/src/fingerprint.ts), so Synaxis and Tandem can share a room.

#include "synaxis/catalog.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace synaxis {

// What a room's `mediaId` says about a local file.
struct MediaId {
    std::string hash;       // 32 hex digits
    std::uintmax_t size = 0;
    std::int64_t duration = 0;  // whole seconds

    bool operator==(const MediaId&) const = default;
};

// "v1.<hash>.<size>.<seconds>"
std::string FormatMediaId(const MediaId& id);
std::optional<MediaId> ParseMediaId(const std::string& text);

// First 32 hex digits of the SHA-256 of the file's first and last 4 MB (of
// all of it, if it's 8 MB or smaller). nullopt if it can't be read.
std::optional<std::string> HashEnds(const std::filesystem::path& path, std::uintmax_t size);

// Remembers hashes, so a file is read at most once while it's unchanged.
// Thread-safe.
class Fingerprints {
public:
    std::optional<std::string> HashOf(const VideoFile& file);
    std::optional<std::string> MediaIdOf(const VideoFile& file);

    struct Match {
        const VideoFile* exact = nullptr;          // the very same file
        std::vector<const VideoFile*> same_length; // otherwise: probably other copies
    };
    // Looks in the catalog for the file a room is playing. Only files of
    // exactly the right size can be the same file, so only those are hashed.
    Match Find(const Catalog& catalog, const MediaId& wanted);

private:
    std::mutex mu_;
    std::unordered_map<std::string, std::string> cache_;  // "path|size|mtime" -> hash
};

} // namespace synaxis
