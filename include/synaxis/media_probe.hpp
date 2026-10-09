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

#include <filesystem>
#include <optional>
#include <string>

namespace synaxis {

// Stream-level facts a filename can't be trusted for. Read by libavformat
// straight from the container headers, so it's cheap enough to run on every
// new file during a scan.
struct MediaInfo {
    double duration = 0.0;    // seconds; 0 when the container doesn't say
    int width = 0;
    int height = 0;
    std::string video_codec;  // libavcodec short name, e.g. "h264", "hevc"
    bool hdr = false;         // PQ or HLG transfer characteristics

    bool operator==(const MediaInfo&) const = default;
};

// Returns nullopt if the file can't be opened or has no video stream.
std::optional<MediaInfo> ProbeMedia(const std::filesystem::path& path);

// Decodes a representative frame, skipping near-black or flat ones (intros,
// fades, title cards), and writes it to `out` as a JPEG `width` pixels wide.
// `duration` (MediaInfo::duration) picks the seek points. Returns false on
// any failure; `out` is never left half-written.
bool WriteThumbnail(const std::filesystem::path& video, const std::filesystem::path& out,
                    double duration, int width = 960);

} // namespace synaxis
