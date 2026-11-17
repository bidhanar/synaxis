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

#include "synaxis/fingerprint.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <regex>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace synaxis {

namespace {

constexpr std::uintmax_t kChunk = 4 * 1024 * 1024;

// Feeds `length` bytes from `offset` into the hash. False on a read error.
bool HashRange(std::ifstream& in, AVSHA* sha, std::uintmax_t offset, std::uintmax_t length) {
    std::vector<char> buf(64 * 1024);
    in.clear();
    in.seekg(std::streamoff(offset));
    while (length > 0) {
        std::streamsize want = std::streamsize(std::min<std::uintmax_t>(length, buf.size()));
        in.read(buf.data(), want);
        std::streamsize got = in.gcount();
        if (got <= 0) return false;
        av_sha_update(sha, reinterpret_cast<const uint8_t*>(buf.data()), size_t(got));
        length -= std::uintmax_t(got);
    }
    return true;
}

} // namespace

std::string FormatMediaId(const MediaId& id) {
    return "v1." + id.hash + "." + std::to_string(id.size) + "." + std::to_string(id.duration);
}

std::optional<MediaId> ParseMediaId(const std::string& text) {
    static const std::regex re(R"(^v1\.([0-9a-f]{32})\.(\d{1,19})\.(\d{1,12})$)");
    std::smatch m;
    if (!std::regex_match(text, m, re)) return std::nullopt;
    MediaId id;
    id.hash = m[1];
    id.size = std::stoull(m[2]);
    id.duration = std::stoll(m[3]);
    return id;
}

std::optional<std::string> HashEnds(const std::filesystem::path& path, std::uintmax_t size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    AVSHA* sha = av_sha_alloc();
    if (!sha) return std::nullopt;
    av_sha_init(sha, 256);
    bool ok = size <= 2 * kChunk
                  ? HashRange(in, sha, 0, size)
                  : HashRange(in, sha, 0, kChunk) && HashRange(in, sha, size - kChunk, kChunk);
    uint8_t digest[32];
    av_sha_final(sha, digest);
    av_free(sha);
    if (!ok) return std::nullopt;

    char hex[33];
    for (int i = 0; i < 16; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return std::string(hex, 32);
}

std::optional<std::string> Fingerprints::HashOf(const VideoFile& file) {
    std::string key = file.path.string() + "|" + std::to_string(file.size) + "|" +
                      std::to_string(file.mtime);
    {
        std::lock_guard lock(mu_);
        if (auto it = cache_.find(key); it != cache_.end()) return it->second;
    }
    auto hash = HashEnds(file.path, file.size);  // outside the lock: it reads 8 MB
    if (hash) {
        std::lock_guard lock(mu_);
        cache_[key] = *hash;
    }
    return hash;
}

std::optional<std::string> Fingerprints::MediaIdOf(const VideoFile& file) {
    auto hash = HashOf(file);
    if (!hash) return std::nullopt;
    double duration = file.info ? file.info->duration : 0;
    return FormatMediaId({*hash, file.size, std::llround(duration)});
}

Fingerprints::Match Fingerprints::Find(const Catalog& catalog, const MediaId& wanted) {
    Match match;
    for (const VideoFile& file : catalog.files()) {
        if (file.size != wanted.size) continue;
        if (HashOf(file) == wanted.hash) {
            match.exact = &file;
            return match;
        }
    }
    if (wanted.duration <= 0) return match;
    for (const VideoFile& file : catalog.files()) {
        if (!file.info || file.info->duration <= 0) continue;
        if (std::abs(file.info->duration - double(wanted.duration)) <= 1.0) {
            match.same_length.push_back(&file);
        }
    }
    return match;
}

} // namespace synaxis
