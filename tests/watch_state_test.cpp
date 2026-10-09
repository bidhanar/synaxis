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

#include "synaxis/watch_state.hpp"

namespace {

namespace fs = std::filesystem;

const char* CurrentTestName() {
    return ::testing::UnitTest::GetInstance()->current_test_info()->name();
}

using synaxis::IsFinished;
using synaxis::Profile;
using synaxis::Progress;
using synaxis::WatchState;

class WatchStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Unique per test: ctest runs each test in its own process, in parallel.
        path_ = fs::temp_directory_path() /
                ("synaxis_state_" + std::string(CurrentTestName()) + "_" +
                 std::to_string(getpid()) + ".json");
        fs::remove(path_);
    }
    void TearDown() override { fs::remove(path_); }

    fs::path path_;
};

TEST(IsFinishedTest, CountsTheCreditsAsTheEnd) {
    EXPECT_FALSE(IsFinished(0, 0));
    EXPECT_FALSE(IsFinished(1000, 3600));
    EXPECT_TRUE(IsFinished(3420, 3600));  // last 5%
    EXPECT_FALSE(IsFinished(3400, 3600));
    EXPECT_TRUE(IsFinished(275, 300));    // last 30 seconds of a short video
}

TEST_F(WatchStateTest, PersistsEverythingAcrossReloads) {
    std::string id;
    {
        WatchState state(path_);
        id = state.AddProfile("Sam", 3).id;
        state.RecordStart(id, "f1", 100);
        state.RecordPosition(id, "f1", 600, 3600, 101);
        EXPECT_TRUE(state.ToggleMyList(id, "t1", 102));
        state.Dismiss(id, "t2", 103);
        state.Flush();
    }

    WatchState reloaded(path_);
    auto p = reloaded.profile(id);
    ASSERT_TRUE(p);
    EXPECT_EQ(p->name, "Sam");
    EXPECT_EQ(p->avatar, 3);
    EXPECT_EQ(p->progress.at("f1"), (Progress{600, 3600, 101, false, 1}));
    EXPECT_EQ(p->my_list.at("t1"), 102);
    EXPECT_EQ(p->dismissed.at("t2"), 103);
}

TEST_F(WatchStateTest, PositionNearTheEndMarksCompleted) {
    WatchState state(path_);
    std::string id = state.AddProfile("A", 0).id;
    state.RecordPosition(id, "f", 3500, 3600, 1);
    EXPECT_TRUE(state.profile(id)->progress.at("f").completed);
    state.RecordPosition(id, "f", 10, 0, 2);  // rewatching; duration kept
    Progress p = state.profile(id)->progress.at("f");
    EXPECT_FALSE(p.completed);
    EXPECT_EQ(p.duration, 3600);
}

TEST_F(WatchStateTest, SetWatchedMarksAndClears) {
    WatchState state(path_);
    std::string id = state.AddProfile("A", 0).id;
    state.RecordPosition(id, "a", 100, 1000, 1);
    state.SetWatched(id, {"a", "b"}, true, 5);
    Profile p = *state.profile(id);
    EXPECT_TRUE(p.progress.at("a").completed);
    EXPECT_EQ(p.progress.at("a").position, 1000);
    EXPECT_TRUE(p.progress.at("b").completed);

    state.SetWatched(id, {"a"}, false, 6);
    EXPECT_EQ(state.profile(id)->progress.count("a"), 0u);
}

TEST_F(WatchStateTest, MyListToggles) {
    WatchState state(path_);
    std::string id = state.AddProfile("A", 0).id;
    EXPECT_TRUE(state.ToggleMyList(id, "t", 1));
    EXPECT_FALSE(state.ToggleMyList(id, "t", 2));
    EXPECT_TRUE(state.profile(id)->my_list.empty());
}

TEST_F(WatchStateTest, ProfilesAreIndependentAndRemovable) {
    WatchState state(path_);
    std::string a = state.AddProfile("A", 0).id;
    std::string b = state.AddProfile("B", 1).id;
    EXPECT_NE(a, b);
    state.RecordPosition(a, "f", 100, 1000, 1);
    EXPECT_TRUE(state.profile(b)->progress.empty());

    EXPECT_TRUE(state.UpdateProfile(b, "Bee", 7));
    EXPECT_EQ(state.profile(b)->name, "Bee");
    EXPECT_TRUE(state.RemoveProfile(a));
    EXPECT_FALSE(state.RemoveProfile(a));
    ASSERT_EQ(state.profiles().size(), 1u);
    EXPECT_EQ(state.profiles()[0].id, b);
}

TEST_F(WatchStateTest, UnknownProfileIsIgnored) {
    WatchState state(path_);
    state.RecordPosition("ghost", "f", 1, 2, 3);
    EXPECT_FALSE(state.ToggleMyList("ghost", "t", 1));
    EXPECT_TRUE(state.profiles().empty());
}

} // namespace
