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

#include <algorithm>

#include "synaxis/vlc_player.hpp"

namespace {

using synaxis::ParseVlcStatus;
using synaxis::PlaylistItem;
using synaxis::VlcCommandLine;
using synaxis::VlcOptions;

TEST(ParseVlcStatusTest, ReadsStateTimeAndFilename) {
    // Trimmed from a real VLC 3 /requests/status.json response.
    const std::string body = R"({
        "fullscreen": false, "state": "playing", "length": 2580, "time": 61,
        "position": 0.0240, "volume": 256,
        "information": {"category": {"meta": {"filename": "Show.S01E02.mkv", "title": "x"}}}
    })";
    auto status = ParseVlcStatus(body);
    ASSERT_TRUE(status);
    EXPECT_EQ(status->state, "playing");
    EXPECT_EQ(status->filename, "Show.S01E02.mkv");
    EXPECT_DOUBLE_EQ(status->length, 2580);
    EXPECT_NEAR(status->time, 61.92, 0.01);  // from position, finer than "time"
}

TEST(ParseVlcStatusTest, ToleratesMissingDetails) {
    auto status = ParseVlcStatus(R"({"state": "stopped", "length": 0, "time": 0})");
    ASSERT_TRUE(status);
    EXPECT_EQ(status->state, "stopped");
    EXPECT_TRUE(status->filename.empty());

    EXPECT_FALSE(ParseVlcStatus("not json"));
    EXPECT_FALSE(ParseVlcStatus(R"({"volume": 1})"));
}

TEST(VlcCommandLineTest, ResumesOnlyTheFirstItemAndTitlesEach) {
    std::vector<PlaylistItem> playlist = {
        {"a", "/v/Show.S01E02.mkv", "Show · S1:E2", 125.5},
        {"b", "/v/Show.S01E03.mkv", "Show · S1:E3", 0},
    };
    std::vector<std::string> argv = VlcCommandLine({"vlc", true}, playlist, 4321, "pw");

    auto has = [&](const std::string& arg) {
        return std::find(argv.begin(), argv.end(), arg) != argv.end();
    };
    EXPECT_EQ(argv.front(), "vlc");
    EXPECT_TRUE(has("--http-port=4321"));
    EXPECT_TRUE(has("--http-password=pw"));
    EXPECT_TRUE(has("--http-host=127.0.0.1"));
    EXPECT_TRUE(has("--fullscreen"));
    EXPECT_TRUE(has("--no-one-instance"));

    auto first = std::find(argv.begin(), argv.end(), "/v/Show.S01E02.mkv");
    ASSERT_NE(first, argv.end());
    EXPECT_EQ(*(first + 1), ":start-time=125.5");
    EXPECT_EQ(*(first + 2), ":meta-title=Show · S1:E2");
    EXPECT_EQ(*(first + 3), "/v/Show.S01E03.mkv");
    EXPECT_EQ(*(first + 4), ":meta-title=Show · S1:E3");
    EXPECT_EQ(std::count_if(argv.begin(), argv.end(),
                            [](const std::string& a) { return a.rfind(":start-time", 0) == 0; }),
              1);

    std::vector<std::string> windowed = VlcCommandLine({"vlc", false}, playlist, 1, "x");
    EXPECT_EQ(std::count(windowed.begin(), windowed.end(), "--fullscreen"), 0);
}

} // namespace
