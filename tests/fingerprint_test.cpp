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

#include "synaxis/fingerprint.hpp"
#include "synaxis/position_estimator.hpp"

namespace {

namespace fs = std::filesystem;
using namespace synaxis;

class FingerprintTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("synaxis_fp_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        fs::create_directories(dir_);
    }
    void TearDown() override { fs::remove_all(dir_); }

    fs::path Write(const std::string& name, const std::string& content) {
        fs::path p = dir_ / name;
        std::ofstream(p, std::ios::binary) << content;
        return p;
    }

    fs::path dir_;
};

TEST(MediaIdTest, FormatsAndParsesTandemsFormat) {
    MediaId id{"0123456789abcdef0123456789abcdef", 1234567890, 5400};
    EXPECT_EQ(FormatMediaId(id), "v1.0123456789abcdef0123456789abcdef.1234567890.5400");
    EXPECT_EQ(ParseMediaId(FormatMediaId(id)), id);
}

TEST(MediaIdTest, RejectsOtherIds) {
    EXPECT_FALSE(ParseMediaId("dQw4w9WgXcQ"));  // a YouTube video
    EXPECT_FALSE(ParseMediaId("v1.0123.10.10"));
    EXPECT_FALSE(ParseMediaId("v2.0123456789abcdef0123456789abcdef.1.1"));
    EXPECT_FALSE(ParseMediaId("v1.0123456789ABCDEF0123456789abcdef.1.1"));
}

// Expected values are SHA-256 from Python's hashlib (first 32 hex digits),
// the same as Tandem's Node implementation produces.
TEST_F(FingerprintTest, HashesAllOfASmallFile) {
    fs::path p = Write("small.mkv", "hello synaxis");
    EXPECT_EQ(HashEnds(p, fs::file_size(p)), "080fd11cbcc5c3590c75e91c5fd447d7");
}

TEST_F(FingerprintTest, HashesTheFirstAndLastFourMegabytesOfALargeFile) {
    std::string big(9 * 1024 * 1024, '\0');
    for (size_t i = 0; i < big.size(); ++i) big[i] = char(i % 251);
    fs::path p = Write("big.mkv", big);
    EXPECT_EQ(HashEnds(p, big.size()), "cdeb6f24f72cec5b6e92f3e74bd0e207");
}

TEST_F(FingerprintTest, MissingFileHasNoHash) {
    EXPECT_FALSE(HashEnds(dir_ / "nope.mkv", 100));
}

TEST_F(FingerprintTest, FindsTheSameFileOrOnesOfTheSameLength) {
    auto file = [&](const std::string& name, const std::string& content, double duration) {
        VideoFile f;
        f.path = Write(name, content);
        f.id = FileId(f.path);
        f.size = content.size();
        f.info = MediaInfo{duration, 1920, 1080, "h264", false};
        return f;
    };
    Catalog catalog({file("Film.2010.1080p.mkv", "the film", 6000.4),
                     file("Film.2010.720p.mkv", "a smaller copy", 6000.9),
                     file("Other.2011.mkv", "the film", 4000)});  // same size, other content

    Fingerprints fingerprints;
    const VideoFile* film = catalog.FindFile(FileId(dir_ / "Film.2010.1080p.mkv"));
    ASSERT_TRUE(film);
    auto media_id = fingerprints.MediaIdOf(*film);
    ASSERT_TRUE(media_id);
    auto wanted = ParseMediaId(*media_id);
    ASSERT_TRUE(wanted);
    EXPECT_EQ(wanted->duration, 6000);

    auto match = fingerprints.Find(catalog, *wanted);
    // "Other" has the same size and content hash too (identical bytes), so
    // the first one found by size and hash wins; both are the same video.
    ASSERT_TRUE(match.exact);
    EXPECT_EQ(match.exact->size, film->size);

    MediaId elsewhere = *wanted;
    elsewhere.hash = "ffffffffffffffffffffffffffffffff";
    match = fingerprints.Find(catalog, elsewhere);
    EXPECT_FALSE(match.exact);
    ASSERT_EQ(match.same_length.size(), 2u);  // both 6000 s copies
}

TEST(PositionEstimatorTest, CountsForwardBetweenVlcsSteps) {
    using namespace std::chrono_literals;
    PositionEstimator e(100ms);
    auto t = PositionEstimator::Clock::time_point{} + 10s;
    e.Update(10.0, true, 1.0, t);
    // The value changed about half a poll before we saw it.
    EXPECT_NEAR(e.At(t).position, 10.05, 1e-6);
    // VLC still says 10.0 a little later: we count on from the change.
    e.Update(10.0, true, 1.0, t + 200ms);
    EXPECT_NEAR(e.At(t + 300ms).position, 10.35, 1e-6);
    EXPECT_FALSE(e.At(t + 300ms).stalled);
}

TEST(PositionEstimatorTest, PausedIsWhereVlcSays) {
    using namespace std::chrono_literals;
    PositionEstimator e(100ms);
    auto t = PositionEstimator::Clock::time_point{} + 10s;
    e.Update(42.0, false, 1.0, t);
    EXPECT_DOUBLE_EQ(e.At(t + 5s).position, 42.0);
}

TEST(PositionEstimatorTest, NotMovingWhilePlayingIsAStall) {
    using namespace std::chrono_literals;
    PositionEstimator e(100ms);
    auto t = PositionEstimator::Clock::time_point{} + 10s;
    e.Update(5.0, true, 1.0, t);
    e.Update(5.0, true, 1.0, t + 2s);
    auto estimate = e.At(t + 2s);
    EXPECT_TRUE(estimate.stalled);
    EXPECT_DOUBLE_EQ(estimate.position, 5.0);
}

TEST(PositionEstimatorTest, FollowsTheRate) {
    using namespace std::chrono_literals;
    PositionEstimator e(100ms);
    auto t = PositionEstimator::Clock::time_point{} + 10s;
    e.Update(0.0, true, 1.1, t);
    EXPECT_NEAR(e.At(t + 950ms).position, 1.1, 1e-6);
}

} // namespace
