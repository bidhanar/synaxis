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

#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace synaxis {

// How far a profile got through one file.
struct Progress {
    double position = 0;       // seconds
    double duration = 0;       // seconds; 0 if unknown
    std::int64_t updated = 0;  // unix seconds of the last change
    bool completed = false;
    int plays = 0;             // times playback was started

    bool operator==(const Progress&) const = default;
};

// Whether playback at `position` counts as having watched the whole thing:
// within the last 5% (end credits) or the last 30 seconds.
bool IsFinished(double position, double duration);

// Resume points below this are treated as "not really started".
inline constexpr double kMinResumeSeconds = 10;

struct Profile {
    std::string id;
    std::string name;
    int avatar = 0;  // index into the UI's avatar palette

    std::map<std::string, Progress> progress;        // by file id
    std::map<std::string, std::int64_t> my_list;     // title id -> time added
    std::map<std::string, std::int64_t> dismissed;   // title id -> time removed
                                                     // from Continue Watching
};

// Per-profile viewing history, persisted to a JSON file. Thread-safe;
// accessors return copies so callers never hold internal references.
class WatchState {
public:
    // Loads `path` if it exists. Nothing is written until the first change.
    explicit WatchState(std::filesystem::path path);
    ~WatchState();

    std::vector<Profile> profiles() const;
    std::optional<Profile> profile(const std::string& id) const;

    Profile AddProfile(const std::string& name, int avatar);
    bool UpdateProfile(const std::string& id, const std::string& name, int avatar);
    bool RemoveProfile(const std::string& id);

    // Playback of `file_id` began.
    void RecordStart(const std::string& profile, const std::string& file_id, std::int64_t now);
    // Periodic position report while playing. Writes to disk are throttled;
    // call Flush() when playback ends.
    void RecordPosition(const std::string& profile, const std::string& file_id, double position,
                        double duration, std::int64_t now);
    // Mark files watched (position jumps to the end) or unwatched (history
    // for them is cleared).
    void SetWatched(const std::string& profile, const std::vector<std::string>& file_ids,
                    bool watched, std::int64_t now);

    // Returns whether the title is in the list afterwards.
    bool ToggleMyList(const std::string& profile, const std::string& title_id, std::int64_t now);
    // Hides a title from Continue Watching until it's played again.
    void Dismiss(const std::string& profile, const std::string& title_id, std::int64_t now);

    void Flush();

private:
    Profile* FindLocked(const std::string& id);
    void SaveLocked();

    mutable std::mutex mu_;
    std::filesystem::path path_;
    std::vector<Profile> profiles_;
    bool dirty_ = false;
    std::int64_t last_save_ = 0;
};

} // namespace synaxis
