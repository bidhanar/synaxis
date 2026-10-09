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

#include "synaxis/feed.hpp"

namespace {

using synaxis::Catalog;
using synaxis::ComputeUpNext;
using synaxis::ContinueWatching;
using synaxis::FileId;
using synaxis::FilenameParser;
using synaxis::MediaInfo;
using synaxis::Profile;
using synaxis::Progress;
using synaxis::Title;
using synaxis::UpNext;
using synaxis::VideoFile;

using Reason = UpNext::Reason;

VideoFile MakeFile(const std::string& path) {
    VideoFile f;
    f.path = path;
    f.id = FileId(path);
    f.metadata = FilenameParser::parse(std::filesystem::path(path).filename().string());
    f.probed = true;
    f.info = MediaInfo{1000, 1920, 1080, "h264", false};
    return f;
}

class FeedTest : public ::testing::Test {
protected:
    FeedTest()
        : e1_(MakeFile("/lib/Show.S01E01.mkv")),
          e2_(MakeFile("/lib/Show.S01E02.mkv")),
          e3_(MakeFile("/lib/Show.S01E03.mkv")),
          movie_(MakeFile("/lib/Film.2010.mkv")),
          catalog_({e1_, e2_, e3_, movie_}) {
        profile_.id = "p";
        profile_.name = "Test";
    }

    const Title& show() const { return *catalog_.Locate(e1_.id)->first; }
    const Title& film() const { return *catalog_.Locate(movie_.id)->first; }

    void Watched(const VideoFile& f, double position, std::int64_t when) {
        profile_.progress[f.id] =
            Progress{position, 1000, when, synaxis::IsFinished(position, 1000), 1};
    }

    VideoFile e1_, e2_, e3_, movie_;
    Catalog catalog_;
    Profile profile_;
};

TEST_F(FeedTest, NeverWatchedStartsAtTheFirstEpisode) {
    UpNext up = ComputeUpNext(catalog_, profile_, show());
    EXPECT_EQ(up.reason, Reason::kStart);
    EXPECT_EQ(up.file_id, e1_.id);
    EXPECT_EQ(up.position, 0);
    EXPECT_EQ(up.duration, 1000);
    EXPECT_EQ(up.last_activity, 0);
}

TEST_F(FeedTest, ResumesAPartlyWatchedEpisode) {
    Watched(e1_, 1000, 10);
    Watched(e2_, 400, 20);
    UpNext up = ComputeUpNext(catalog_, profile_, show());
    EXPECT_EQ(up.reason, Reason::kResume);
    EXPECT_EQ(up.file_id, e2_.id);
    EXPECT_EQ(up.position, 400);
    EXPECT_EQ(up.last_activity, 20);
}

TEST_F(FeedTest, BarelyStartedRestartsThatEpisode) {
    Watched(e2_, 4, 20);
    UpNext up = ComputeUpNext(catalog_, profile_, show());
    EXPECT_EQ(up.reason, Reason::kStart);
    EXPECT_EQ(up.file_id, e2_.id);
    EXPECT_EQ(up.position, 0);
}

TEST_F(FeedTest, FinishingAnEpisodeMovesToTheNext) {
    Watched(e1_, 990, 10);
    UpNext up = ComputeUpNext(catalog_, profile_, show());
    EXPECT_EQ(up.reason, Reason::kNextEpisode);
    EXPECT_EQ(up.file_id, e2_.id);
    EXPECT_EQ(up.item, 1u);
}

TEST_F(FeedTest, TheMostRecentActivityWinsOverEpisodeOrder) {
    Watched(e3_, 1000, 10);
    Watched(e1_, 500, 30);  // went back to rewatch the pilot
    UpNext up = ComputeUpNext(catalog_, profile_, show());
    EXPECT_EQ(up.reason, Reason::kResume);
    EXPECT_EQ(up.file_id, e1_.id);
}

TEST_F(FeedTest, FinishingEverythingOffersARewatch) {
    Watched(e3_, 1000, 10);
    UpNext up = ComputeUpNext(catalog_, profile_, show());
    EXPECT_EQ(up.reason, Reason::kRewatch);
    EXPECT_EQ(up.file_id, e1_.id);
    EXPECT_EQ(up.position, 0);

    Watched(movie_, 1000, 10);
    EXPECT_EQ(ComputeUpNext(catalog_, profile_, film()).reason, Reason::kRewatch);
}

TEST_F(FeedTest, ContinueWatchingIsMostRecentFirstAndSkipsFinishedTitles) {
    Watched(e1_, 1000, 10);   // show: next episode pending
    Watched(movie_, 300, 50); // movie: in progress, more recent
    std::vector<UpNext> rows = ContinueWatching(catalog_, profile_);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].title, &film());
    EXPECT_EQ(rows[1].title, &show());

    Watched(movie_, 1000, 60);  // finished the movie
    rows = ContinueWatching(catalog_, profile_);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].title, &show());
}

TEST_F(FeedTest, DismissedTitlesReturnOnlyAfterNewActivity) {
    Watched(movie_, 300, 50);
    profile_.dismissed[film().id] = 60;
    EXPECT_TRUE(ContinueWatching(catalog_, profile_).empty());

    Watched(movie_, 350, 70);
    EXPECT_EQ(ContinueWatching(catalog_, profile_).size(), 1u);
}

} // namespace
