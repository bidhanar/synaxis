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

#include "synaxis/library_service.hpp"

#include "synaxis/media_probe.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace synaxis {

namespace fs = std::filesystem;

namespace {

std::int64_t ToUnix(fs::file_time_type t) {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::file_clock::to_sys(t).time_since_epoch())
        .count();
}

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Publish partial results this often while probing, so a big first scan
// fills the UI progressively.
constexpr size_t kPublishEvery = 50;

} // namespace

LibraryService::LibraryService(fs::path data_dir)
    : data_dir_(std::move(data_dir)), thumbs_dir_(data_dir_ / "thumbs") {
    std::error_code ec;
    fs::create_directories(thumbs_dir_, ec);
    catalog_ = std::make_shared<const Catalog>(LoadVideoFiles(data_dir_ / "catalog.json"));
    progress_.files = catalog_->files().size();
    progress_.version = 1;
    worker_ = std::thread([this] { Worker(); });
}

LibraryService::~LibraryService() {
    stop_ = true;
    wake_.notify_all();
    worker_.join();
}

std::shared_ptr<const Catalog> LibraryService::catalog() const {
    std::lock_guard lock(mu_);
    return catalog_;
}

ScanProgress LibraryService::progress() const {
    std::lock_guard lock(mu_);
    return progress_;
}

void LibraryService::Rescan(ScanOptions options) {
    {
        std::lock_guard lock(mu_);
        pending_ = std::move(options);
        failed_thumbs_.clear();  // a manual rescan retries everything
        progress_.running = true;
    }
    wake_.notify_all();
}

fs::path LibraryService::ThumbnailPath(const std::string& file_id) const {
    return thumbs_dir_ / (file_id + ".jpg");
}

std::optional<std::int64_t> LibraryService::ThumbnailVersion(const std::string& file_id) const {
    std::error_code ec;
    auto t = fs::last_write_time(ThumbnailPath(file_id), ec);
    if (ec) return std::nullopt;
    return ToUnix(t);
}

void LibraryService::Worker() {
    std::unique_lock lock(mu_);
    while (!stop_) {
        wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
        if (stop_) break;
        ScanOptions options = std::move(*pending_);
        pending_.reset();

        lock.unlock();
        RunScan(options);
        lock.lock();

        if (!pending_) {
            progress_.running = false;
            progress_.phase = "idle";
            progress_.done = progress_.total = 0;
            progress_.finished_at = UnixNow();
        }
    }
}

void LibraryService::SetPhase(const std::string& phase, size_t total) {
    std::lock_guard lock(mu_);
    progress_.phase = phase;
    progress_.done = 0;
    progress_.total = total;
}

void LibraryService::Publish(std::vector<VideoFile> files) {
    SaveVideoFiles(files, data_dir_ / "catalog.json");
    auto catalog = std::make_shared<const Catalog>(std::move(files));
    std::lock_guard lock(mu_);
    catalog_ = std::move(catalog);
    progress_.files = catalog_->files().size();
    progress_.version++;
}

void LibraryService::RunScan(const ScanOptions& options) {
    auto interrupted = [this] {
        std::lock_guard lock(mu_);
        return stop_ || pending_.has_value();
    };

    SetPhase("scanning", 0);
    std::vector<VideoFile> files = ScanVideoFiles(options, &stop_);
    if (interrupted()) return;

    // Unchanged files keep their probe results from the previous catalog.
    std::shared_ptr<const Catalog> previous = catalog();
    std::unordered_map<std::string, const VideoFile*> known;
    for (const VideoFile& f : previous->files()) known[f.id] = &f;

    std::vector<size_t> to_probe;
    for (size_t i = 0; i < files.size(); ++i) {
        auto it = known.find(files[i].id);
        if (it != known.end() && it->second->probed && it->second->size == files[i].size &&
            it->second->mtime == files[i].mtime) {
            files[i].probed = true;
            files[i].info = it->second->info;
        } else {
            to_probe.push_back(i);
        }
    }
    Publish(files);

    SetPhase("reading", to_probe.size());
    for (size_t k = 0; k < to_probe.size(); ++k) {
        if (interrupted()) return;
        VideoFile& f = files[to_probe[k]];
        f.info = ProbeMedia(f.path);
        f.probed = true;
        {
            std::lock_guard lock(mu_);
            progress_.done = k + 1;
        }
        if ((k + 1) % kPublishEvery == 0) Publish(files);
    }
    if (!to_probe.empty()) Publish(std::move(files));

    RenderThumbnails();
}

void LibraryService::RenderThumbnails() {
    std::shared_ptr<const Catalog> catalog = this->catalog();

    std::vector<const VideoFile*> todo;
    {
        std::lock_guard lock(mu_);
        for (const VideoFile& f : catalog->files()) {
            if (failed_thumbs_.count(f.id)) continue;
            auto version = ThumbnailVersion(f.id);
            if (!version || *version < f.mtime) todo.push_back(&f);
        }
    }
    // Newest first: they're the first thing the home screen shows.
    std::sort(todo.begin(), todo.end(),
              [](const VideoFile* a, const VideoFile* b) { return a->mtime > b->mtime; });

    SetPhase("thumbnails", todo.size());
    for (const VideoFile* f : todo) {
        {
            std::lock_guard lock(mu_);
            if (stop_ || pending_) return;
        }
        double duration = f->info ? f->info->duration : 0;
        bool ok = WriteThumbnail(f->path, ThumbnailPath(f->id), duration);
        std::lock_guard lock(mu_);
        if (!ok) failed_thumbs_.insert(f->id);
        progress_.done++;
    }

    // Thumbnails of files no longer in the library.
    std::unordered_set<std::string> live;
    for (const VideoFile& f : catalog->files()) live.insert(f.id + ".jpg");
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(thumbs_dir_, ec)) {
        if (!live.count(entry.path().filename().string())) fs::remove(entry.path(), ec);
    }
}

} // namespace synaxis
