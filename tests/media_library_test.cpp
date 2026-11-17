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

#include <gtest/gtest.h>

#include "fixtures.hpp"

#include <algorithm>
#include <filesystem>

#include "synaxis/media_library.hpp"

namespace {

namespace fs = std::filesystem;

using synaxis::MediaEntry;
using synaxis::MediaLibrary;

fs::path FixturesRoot() {
    return FixturesDir() / "media_library";
}

TEST(MediaLibraryTest, ScanResolvesCanonicalPathsAndParsesMetadata) {
    std::vector<MediaEntry> entries = MediaLibrary::Scan(FixturesRoot());

    ASSERT_FALSE(entries.empty());
    for (const auto& entry : entries) {
        EXPECT_TRUE(entry.path.is_absolute()) << entry.path;
        EXPECT_TRUE(fs::exists(entry.path)) << entry.path;
    }

    auto it = std::find_if(entries.begin(), entries.end(), [](const MediaEntry& e) {
        return e.path.filename() == "Inception.2010.1080p.BluRay.x264-SPARKS.mp4";
    });
    ASSERT_NE(it, entries.end());
    EXPECT_EQ(it->metadata.title, "Inception");
    EXPECT_EQ(it->metadata.year, 2010);
}

TEST(MediaLibraryTest, SaveAndLoadRoundTrips) {
    std::vector<MediaEntry> original = MediaLibrary::Scan(FixturesRoot());

    fs::path tmp = fs::temp_directory_path() / "synaxis_library_test.json";
    MediaLibrary::SaveToJson(original, tmp);
    std::vector<MediaEntry> loaded = MediaLibrary::LoadFromJson(tmp);
    fs::remove(tmp);

    EXPECT_EQ(original, loaded);
}

} // namespace
