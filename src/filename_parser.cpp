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

#include "synaxis/filename_parser.hpp"
#include <algorithm>
#include <filesystem>
#include <regex>

namespace synaxis {

namespace {

// Strips a leading "[GROUP] " release-group tag and normalizes separators
// left over from the scene-release naming convention into a clean title.
std::string CleanTitle(std::string raw) {
    static const std::regex kLeadingBracket(R"(^\s*\[[^\]]*\]\s*)");
    raw = std::regex_replace(raw, kLeadingBracket, "");

    std::replace(raw.begin(), raw.end(), '.', ' ');
    std::replace(raw.begin(), raw.end(), '_', ' ');

    static const std::regex kExtraSpace(R"(\s+)");
    raw = std::regex_replace(raw, kExtraSpace, " ");

    auto is_trim_char = [](char c) {
        return c == ' ' || c == '-' || c == '.' || c == '_';
    };
    auto begin = raw.begin();
    while (begin != raw.end() && is_trim_char(*begin)) ++begin;
    auto end = raw.end();
    while (end != begin && is_trim_char(*(end - 1))) --end;

    return std::string(begin, end);
}

} // namespace

ParsedFilename FilenameParser::parse(const std::string& filename) {
    ParsedFilename result{};

    // Leftmost marker position wins — it marks where the title ends and
    // release metadata begins, regardless of which kind of marker it is.
    std::optional<size_t> title_cutoff;
    auto consider_cutoff = [&](size_t pos) {
        if (!title_cutoff || pos < *title_cutoff) title_cutoff = pos;
    };

    // "S01E01" / "s01e06" style.
    static const std::regex kStandardPattern(R"([Ss](\d{1,2})[Ee](\d{1,2}))");
    // "1x04" / "2x01" style. \b keeps it from matching inside tokens like
    // "x264" (no digit precedes the 'x' there).
    static const std::regex kAltPattern(R"(\b(\d{1,2})x(\d{2})\b)");

    std::smatch match;
    std::optional<size_t> episode_marker_end;
    if (std::regex_search(filename, match, kStandardPattern) ||
        std::regex_search(filename, match, kAltPattern)) {
        result.season = std::stoi(match[1]);
        result.episode = std::stoi(match[2]);
        consider_cutoff(match.position(0));
        episode_marker_end = match.position(0) + match.length(0);
    }

    // Release year — movies don't have a season/episode marker, so this is
    // what tells the title where to stop instead. Optional surrounding
    // parens are consumed too, e.g. "(2015)".
    static const std::regex kYearPattern(R"(\(?\b(19\d{2}|20\d{2})\b\)?)");
    if (std::regex_search(filename, match, kYearPattern)) {
        result.year = std::stoi(match[1]);
        consider_cutoff(match.position(0));
    }

    // Rip source — describes how the file was captured, not something
    // an encoded stream can reveal, so it must come from the filename.
    static const std::regex kSourcePattern(
        R"(\b(WEB-DL|WEBRip|BluRay|BrRip|BDRip|DVDRip|HDRip|HDTV|HDTS|CAM)\b)",
        std::regex::icase);
    if (std::regex_search(filename, match, kSourcePattern)) {
        result.source = match[1];
        consider_cutoff(match.position(0));
    }

    // Edition/release tags — proper/repack fixes, theatrical cuts, HDR
    // masters, streaming-service origin. None of this is in the stream.
    static const std::regex kTagPattern(
        R"(\b(PROPER|REPACK|IMAX|HDR|AMZN|NF)\b)",
        std::regex::icase);
    for (auto it = std::sregex_iterator(filename.begin(), filename.end(), kTagPattern);
         it != std::sregex_iterator(); ++it) {
        result.tags.push_back((*it)[1]);
        consider_cutoff(it->position(0));
    }

    std::string raw_title = title_cutoff ? filename.substr(0, *title_cutoff)
                                          : std::filesystem::path(filename).stem().string();
    std::string title = CleanTitle(raw_title);
    if (!title.empty()) result.title = title;

    // Episode name: whatever sits between the episode marker and the first
    // piece of release metadata after it (resolution, codec, source, tags,
    // a bracketed group...).
    const std::string stem = std::filesystem::path(filename).stem().string();
    if (episode_marker_end && *episode_marker_end < stem.size()) {
        static const std::regex kEpisodeTitleEnd(
            R"(\[|\(|\b(?:\d{3,4}[pi]|[xh]\.?26[45]|HEVC|XviD|10.?bit|(?:19|20)\d{2}|)"
            R"(WEB-DL|WEBRip|BluRay|BrRip|BDRip|DVDRip|HDRip|HDTV|HDTS|CAM|)"
            R"(PROPER|REPACK|IMAX|HDR|AMZN|NF|UHD)\b)",
            std::regex::icase);
        std::string rest = stem.substr(*episode_marker_end);
        if (std::regex_search(rest, match, kEpisodeTitleEnd)) rest.resize(match.position(0));
        std::string episode_title = CleanTitle(rest);
        if (!episode_title.empty()) result.episode_title = episode_title;
    }

    return result;
}

} // namespace synaxis
