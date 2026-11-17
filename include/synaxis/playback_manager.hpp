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

#include "synaxis/position_estimator.hpp"
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

    // What a Watch Together session's VLC is doing, for the sync logic in
    // the page. The position is smoothed and current as of the call.
    struct SyncedReading {
        bool open = false;   // a Watch Together VLC window is open
        bool ready = false;  // ...and has answered at least once
        std::string file_id;
        std::string state;   // "playing", "paused" or "stopped"
        double position = 0;
        double length = 0;
        double rate = 1;
        bool stalled = false;  // says it's playing, but the position isn't moving
    };

    explicit PlaybackManager(WatchState& state);
    ~PlaybackManager();  // stops following playback; leaves VLC running

    // Starts VLC for `profile_id`, closing any session already open.
    // A `synced` session (Watch Together) is followed closely, ten times a
    // second, and can be controlled with Control().
    bool Play(const std::string& profile_id, std::vector<PlaylistItem> playlist,
              const VlcOptions& options, std::string* error, bool synced = false);
    void Stop();
    // Closes the open session only if it's a Watch Together one.
    void StopSynced();
    std::optional<NowPlaying> now_playing() const;

    SyncedReading synced_reading() const;
    // "play", "pause", "seek" (to `value` seconds) or "rate" (`value`).
    // False if no Watch Together session is open or VLC didn't answer.
    bool Control(const std::string& command, double value = 0);

private:
    static constexpr std::chrono::milliseconds kSyncedPoll{100};

    struct Session {
        std::unique_ptr<VlcSession> vlc;
        std::string profile_id;
        std::string current_file;  // last file seen playing
        std::chrono::steady_clock::time_point started;
        bool reached_start = false;  // first item has seeked to its resume point

        bool synced = false;
        std::optional<VlcStatus> last;  // latest status (synced sessions only)
        PositionEstimator estimator{kSyncedPoll};
        std::chrono::steady_clock::time_point last_track{};
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
