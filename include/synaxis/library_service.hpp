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

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

namespace synaxis {

struct ScanProgress {
    bool running = false;
    std::string phase = "idle";  // "scanning", "reading", "thumbnails", "idle"
    size_t files = 0;            // video files found by the last walk
    size_t done = 0;             // progress through the current phase
    size_t total = 0;
    std::uint64_t version = 0;   // bumps whenever the catalog changes
    std::int64_t finished_at = 0;
};

// Keeps the catalog current in the background: walks the configured folders,
// probes new or changed files, and renders thumbnails. Readers get immutable
// snapshots, so browsing never waits on a scan.
class LibraryService {
public:
    // Loads the cached catalog from `data_dir`, if any, so the library is
    // browsable immediately.
    explicit LibraryService(std::filesystem::path data_dir);
    ~LibraryService();

    std::shared_ptr<const Catalog> catalog() const;
    ScanProgress progress() const;

    // Queues a scan; one already running finishes its walk first.
    void Rescan(ScanOptions options);

    std::filesystem::path ThumbnailPath(const std::string& file_id) const;
    // Modification time of the thumbnail (for cache-busting URLs), if one exists.
    std::optional<std::int64_t> ThumbnailVersion(const std::string& file_id) const;

private:
    void Worker();
    void RunScan(const ScanOptions& options);
    void RenderThumbnails();
    void Publish(std::vector<VideoFile> files);
    void SetPhase(const std::string& phase, size_t total);

    std::filesystem::path data_dir_;
    std::filesystem::path thumbs_dir_;

    mutable std::mutex mu_;
    std::condition_variable wake_;
    std::shared_ptr<const Catalog> catalog_;
    ScanProgress progress_;
    std::optional<ScanOptions> pending_;
    std::set<std::string> failed_thumbs_;  // don't retry these every scan
    std::atomic<bool> stop_ = false;
    std::thread worker_;
};

} // namespace synaxis
