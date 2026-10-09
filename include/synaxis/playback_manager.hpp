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

#include "synaxis/vlc_player.hpp"
#include "synaxis/watch_state.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace synaxis {

// Runs one VLC session at a time and records the viewer's progress from it
// into WatchState, so resume points and "watched" marks stay current.
class PlaybackManager {
public:
    struct NowPlaying {
        std::string profile_id;
        std::string file_id;
        std::string title;
        double position = 0;
        double duration = 0;
        bool paused = false;
    };

    explicit PlaybackManager(WatchState& state);
    ~PlaybackManager();  // stops following playback; leaves VLC running

    // Starts VLC for `profile_id`, closing any session already open.
    bool Play(const std::string& profile_id, std::vector<PlaylistItem> playlist,
              const VlcOptions& options, std::string* error);
    void Stop();
    std::optional<NowPlaying> now_playing() const;

private:
    struct Session {
        std::unique_ptr<VlcSession> vlc;
        std::string profile_id;
        std::string current_file;  // last file seen playing
        std::chrono::steady_clock::time_point started;
        bool reached_start = false;  // first item has seeked to its resume point
    };

    void Monitor();
    void Track(Session& session, const VlcStatus& status);

    WatchState& state_;
    mutable std::mutex mu_;
    std::condition_variable wake_;
    std::shared_ptr<Session> session_;
    std::optional<NowPlaying> now_playing_;
    bool stopping_ = false;
    std::thread monitor_;
};

} // namespace synaxis
