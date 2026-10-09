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
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "synaxis/catalog.hpp"

namespace {

namespace fs = std::filesystem;

const char* CurrentTestName() {
    return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

using synaxis::Catalog;
using synaxis::FileId;
using synaxis::FilenameParser;
using synaxis::MediaInfo;
using synaxis::Title;
using synaxis::TitleKind;
using synaxis::VideoFile;

VideoFile MakeFile(const std::string& path, int height = 1080,
                   std::uintmax_t size = 1'000'000'000, std::int64_t mtime = 100) {
    VideoFile f;
    f.path = path;
    f.id = FileId(path);
    f.size = size;
    f.mtime = mtime;
    f.metadata = FilenameParser::parse(fs::path(path).filename().string());
    f.probed = true;
    f.info = MediaInfo{2400, height * 16 / 9, height, "h264", false};
    return f;
}

TEST(CatalogTest, GroupsEpisodesIntoOneShowInWatchOrder) {
    Catalog catalog({
        MakeFile("/lib/Show/Season 02/Show.S02E01.1080p.mkv"),
        MakeFile("/lib/Show/Season 01/Show.S01E02.1080p.mkv"),
        MakeFile("/lib/Show/Season 01/show.s01e01.720p.mkv"),
    });

    ASSERT_EQ(catalog.titles().size(), 1u);
    const Title& show = catalog.titles()[0];
    EXPECT_EQ(show.kind, TitleKind::kShow);
    EXPECT_EQ(show.name, "Show");  // capitalised spelling wins
    ASSERT_EQ(show.items.size(), 3u);
    EXPECT_EQ(show.items[0].season, 1);
    EXPECT_EQ(show.items[0].episode, 1);
    EXPECT_EQ(show.items[1].episode, 2);
    EXPECT_EQ(show.items[2].season, 2);
}

TEST(CatalogTest, SpecialsComeAfterRegularSeasons) {
    Catalog catalog({
        MakeFile("/lib/Show.S00E01.mkv"),
        MakeFile("/lib/Show.S01E01.mkv"),
    });
    ASSERT_EQ(catalog.titles().size(), 1u);
    EXPECT_EQ(catalog.titles()[0].items[0].season, 1);
    EXPECT_EQ(catalog.titles()[0].items[1].season, 0);
}

TEST(CatalogTest, MoviesWithTheSameNameButDifferentYearsStaySeparate) {
    Catalog catalog({
        MakeFile("/lib/Heat.1995.1080p.BluRay.mkv"),
        MakeFile("/lib/Heat.2023.1080p.WEB-DL.mkv"),
    });
    ASSERT_EQ(catalog.titles().size(), 2u);
    EXPECT_EQ(catalog.titles()[0].kind, TitleKind::kMovie);
}

TEST(CatalogTest, DuplicateCopiesShareAnItemBestFirst) {
    VideoFile sd = MakeFile("/lib/a/Inception.2010.720p.mkv", 720);
    VideoFile uhd = MakeFile("/lib/b/Inception (2010) [2160p].mkv", 2160);
    Catalog catalog({sd, uhd});

    ASSERT_EQ(catalog.titles().size(), 1u);
    const Title& movie = catalog.titles()[0];
    EXPECT_EQ(movie.year, 2010);
    ASSERT_EQ(movie.items.size(), 1u);
    EXPECT_EQ(movie.items[0].file_ids, (std::vector<std::string>{uhd.id, sd.id}));
}

TEST(CatalogTest, PunctuationAndAmpersandsDoNotSplitAShow) {
    Catalog catalog({
        MakeFile("/lib/Rick.and.Morty.S01E01.mkv"),
        MakeFile("/lib/Rick & Morty - S01E02.mkv"),
    });
    ASSERT_EQ(catalog.titles().size(), 1u);
    EXPECT_EQ(catalog.titles()[0].items.size(), 2u);
}

TEST(CatalogTest, NamesEpisodesWithoutATitleAfterTheirFolder) {
    Catalog catalog({MakeFile("/lib/Cool Show/Season 1/S01E01.mkv")});
    ASSERT_EQ(catalog.titles().size(), 1u);
    EXPECT_EQ(catalog.titles()[0].name, "Cool Show");
}

TEST(CatalogTest, TitleCasesAllLowercaseNames) {
    Catalog catalog({MakeFile("/lib/mad.max.fury.road.2015.2160p.uhd.bluray.x265-evo.mkv")});
    ASSERT_EQ(catalog.titles().size(), 1u);
    EXPECT_EQ(catalog.titles()[0].name, "Mad Max Fury Road");
}

TEST(CatalogTest, LocatesFilesAndTracksNewestAddition) {
    VideoFile e1 = MakeFile("/lib/Show.S01E01.mkv", 1080, 1, 100);
    VideoFile e2 = MakeFile("/lib/Show.S01E02.mkv", 1080, 1, 300);
    Catalog catalog({e1, e2});

    auto loc = catalog.Locate(e2.id);
    ASSERT_TRUE(loc);
    EXPECT_EQ(loc->first->name, "Show");
    EXPECT_EQ(loc->second, 1u);
    EXPECT_EQ(loc->first->added, 300);
    EXPECT_FALSE(catalog.Locate("nope"));
    EXPECT_EQ(catalog.FindTitle(loc->first->id), loc->first);
}

class ScanTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Unique per test: ctest runs each test in its own process, in parallel.
        root_ = fs::temp_directory_path() /
                ("synaxis_scan_" + std::string(CurrentTestName()) + "_" + std::to_string(getpid()));
        fs::remove_all(root_);
        fs::create_directories(root_ / "Movies");
        fs::create_directories(root_ / ".hidden");
    }
    void TearDown() override { fs::remove_all(root_); }

    // Sparse files: big on paper, no real disk use.
    void MakeSized(const fs::path& path, std::uintmax_t size) {
        std::ofstream(path).put('\0');
        fs::resize_file(path, size);
    }

    fs::path root_;
};

TEST_F(ScanTest, KeepsOnlyLargeVideosOutsideHiddenFolders) {
    constexpr std::uintmax_t kMB = 1024 * 1024;
    MakeSized(root_ / "Movies" / "Big.Movie.2020.mkv", 300 * kMB);
    MakeSized(root_ / "Movies" / "sample.mkv", 20 * kMB);
    MakeSized(root_ / "Movies" / "notes.txt", 300 * kMB);
    MakeSized(root_ / ".hidden" / "Secret.2019.mp4", 300 * kMB);
    MakeSized(root_ / "Upper.Case.2018.MKV", 300 * kMB);

    synaxis::ScanOptions options;
    options.roots = {root_, root_ / "Movies"};  // overlapping roots
    options.min_size = 200 * kMB;
    std::vector<VideoFile> files = synaxis::ScanVideoFiles(options);

    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[0].path.filename(), "Big.Movie.2020.mkv");
    EXPECT_EQ(files[1].path.filename(), "Upper.Case.2018.MKV");
    EXPECT_TRUE(files[0].path.is_absolute());
    EXPECT_EQ(files[0].size, 300 * kMB);
    EXPECT_EQ(files[0].metadata.year, 2020);
    EXPECT_FALSE(files[0].probed);
}

TEST_F(ScanTest, MissingRootIsNotAnError) {
    synaxis::ScanOptions options;
    options.roots = {root_ / "does-not-exist"};
    EXPECT_TRUE(synaxis::ScanVideoFiles(options).empty());
}

TEST_F(ScanTest, CacheRoundTrips) {
    std::vector<VideoFile> files = {MakeFile("/lib/Show.S01E01.mkv"), MakeFile("/lib/Movie.2001.mkv")};
    files[1].info.reset();  // probed but unreadable
    synaxis::SaveVideoFiles(files, root_ / "cache.json");
    EXPECT_EQ(synaxis::LoadVideoFiles(root_ / "cache.json"), files);
}

TEST_F(ScanTest, CorruptCacheLoadsEmpty) {
    std::ofstream(root_ / "cache.json") << "{not json";
    EXPECT_TRUE(synaxis::LoadVideoFiles(root_ / "cache.json").empty());
    EXPECT_TRUE(synaxis::LoadVideoFiles(root_ / "missing.json").empty());
}

} // namespace
