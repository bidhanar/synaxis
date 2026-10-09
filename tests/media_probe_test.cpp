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

#include <filesystem>
#include <fstream>

#include "synaxis/media_probe.hpp"

namespace {

namespace fs = std::filesystem;

const fs::path kMovies = fs::path(FIXTURES_DIR) / "media_library" / "Movies";

TEST(MediaProbeTest, ReadsStreamFactsFromARealFile) {
    auto info = synaxis::ProbeMedia(kMovies / "Inception.2010.1080p.BluRay.x264-SPARKS.mp4");
    ASSERT_TRUE(info);
    EXPECT_GT(info->duration, 0);
    EXPECT_GT(info->width, 0);
    EXPECT_GT(info->height, 0);
    EXPECT_FALSE(info->video_codec.empty());
}

TEST(MediaProbeTest, UnreadableFileIsNullopt) {
    // The other fixtures are empty placeholder files.
    EXPECT_FALSE(synaxis::ProbeMedia(kMovies / "The.Long.Drift.2022.HDTS.x264-EVO.mkv"));
    EXPECT_FALSE(synaxis::ProbeMedia(kMovies / "missing.mkv"));
}

TEST(MediaProbeTest, WritesAJpegThumbnail) {
    fs::path video = kMovies / "Inception.2010.1080p.BluRay.x264-SPARKS.mp4";
    fs::path out = fs::temp_directory_path() / "synaxis_thumb_test.jpg";
    fs::remove(out);

    auto info = synaxis::ProbeMedia(video);
    ASSERT_TRUE(info);
    ASSERT_TRUE(synaxis::WriteThumbnail(video, out, info->duration, 320));

    std::ifstream in(out, std::ios::binary);
    unsigned char magic[2] = {};
    in.read(reinterpret_cast<char*>(magic), 2);
    EXPECT_EQ(magic[0], 0xFF);
    EXPECT_EQ(magic[1], 0xD8);
    EXPECT_FALSE(fs::exists(out.string() + ".tmp"));
    fs::remove(out);
}

TEST(MediaProbeTest, ThumbnailOfUnreadableFileFailsCleanly) {
    fs::path out = fs::temp_directory_path() / "synaxis_thumb_fail.jpg";
    fs::remove(out);
    EXPECT_FALSE(synaxis::WriteThumbnail(kMovies / "The.Long.Drift.2022.HDTS.x264-EVO.mkv", out, 0));
    EXPECT_FALSE(fs::exists(out));
}

} // namespace
