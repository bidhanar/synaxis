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

#include <optional>
#include <string>
#include <vector>

namespace synaxis {

// Result of parsing a single media filename. Fields are nullopt/empty when
// no recognized pattern is found. Resolution/codec are intentionally not
// handled here — those are properties of the encoded stream and get
// inspected via libmpv instead. Source (WEB-DL/BluRay/HDTV/CAM/HDTS/...) and
// edition tags (PROPER/REPACK/IMAX/HDR/AMZN/NF/...) describe how the file
// was ripped or released, which isn't recoverable from the stream itself,
// so they have to come from the filename like season/episode. `title` is a
// fallback series/movie name derived from the filename itself, for when the
// user doesn't supply one; `year` (movies) doubles as the signal that lets
// `title` know where to stop when there's no season/episode marker.
// `episode_title` is the episode's own name when the filename carries one
// between the episode marker and the release metadata ("... S01E01 - Pilot
// [720p]"); scene-style names usually don't.
struct ParsedFilename {
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<int> year;
    std::optional<std::string> source;
    std::optional<std::string> title;
    std::vector<std::string> tags;
    std::optional<std::string> episode_title;

    bool operator==(const ParsedFilename&) const = default;
};

class FilenameParser {
public:
    static ParsedFilename parse(const std::string& filename);
};

} // namespace synaxis
