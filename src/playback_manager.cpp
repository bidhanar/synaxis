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

#include "synaxis/playback_manager.hpp"

#include <chrono>

namespace synaxis {

namespace {

using namespace std::chrono_literals;

// How long we wait for VLC to jump to the resume point before trusting
// the positions it reports.
constexpr auto kSeekGrace = 15s;

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

PlaybackManager::PlaybackManager(WatchState& state)
    : state_(state), monitor_([this] { Monitor(); }) {}

PlaybackManager::~PlaybackManager() {
    {
        std::lock_guard lock(mu_);
        stopping_ = true;
    }
    wake_.notify_all();
    monitor_.join();
    state_.Flush();
}

bool PlaybackManager::Play(const std::string& profile_id, std::vector<PlaylistItem> playlist,
                           const VlcOptions& options, std::string* error) {
    // Like switching titles on a TV: the previous session closes first.
    Stop();

    auto session = std::make_shared<Session>();
    NowPlaying now;
    if (!playlist.empty()) {
        now = {profile_id, playlist.front().file_id, playlist.front().title,
               playlist.front().start, 0, false};
    }
    session->vlc = VlcSession::Launch(options, std::move(playlist), error);
    if (!session->vlc) return false;
    session->profile_id = profile_id;
    session->started = std::chrono::steady_clock::now();

    {
        std::lock_guard lock(mu_);
        session_ = std::move(session);
        now_playing_ = now;
    }
    wake_.notify_all();
    return true;
}

void PlaybackManager::Stop() {
    std::shared_ptr<Session> old;
    {
        std::lock_guard lock(mu_);
        old = std::move(session_);
        now_playing_.reset();
    }
    if (old) old->vlc->Terminate();
    state_.Flush();
}

std::optional<PlaybackManager::NowPlaying> PlaybackManager::now_playing() const {
    std::lock_guard lock(mu_);
    return now_playing_;
}

void PlaybackManager::Monitor() {
    std::unique_lock lock(mu_);
    while (!stopping_) {
        wake_.wait_for(lock, 1s, [this] { return stopping_; });
        if (stopping_) break;
        std::shared_ptr<Session> session = session_;
        if (!session) continue;

        // Talking to VLC can take a moment; don't block the UI meanwhile.
        lock.unlock();
        bool running = session->vlc->Running();
        std::optional<VlcStatus> status;
        if (running) status = session->vlc->Poll();
        lock.lock();

        if (session_ != session) continue;  // replaced while we were polling
        if (status) Track(*session, *status);
        if (!running) {
            session_.reset();
            now_playing_.reset();
            state_.Flush();
        }
    }
}

void PlaybackManager::Track(Session& session, const VlcStatus& status) {
    const PlaylistItem* item = session.vlc->Current(status);
    if (!item || status.state == "stopped") return;

    now_playing_ = NowPlaying{session.profile_id, item->file_id, item->title,
                              status.time, status.length, status.state == "paused"};
    if (status.length <= 0) return;

    // VLC reports 0:00 until it has seeked to the resume point; recording
    // that would wipe out the very position we're resuming from.
    if (item == &session.vlc->playlist().front() && item->start > 0 && !session.reached_start) {
        bool seeked = status.time + 5 >= item->start;
        if (!seeked && std::chrono::steady_clock::now() - session.started < kSeekGrace) return;
        session.reached_start = true;
    }

    std::int64_t now = UnixNow();
    if (item->file_id != session.current_file) {
        session.current_file = item->file_id;
        state_.RecordStart(session.profile_id, item->file_id, now);
    }
    state_.RecordPosition(session.profile_id, item->file_id, status.time, status.length, now);
}

} // namespace synaxis
