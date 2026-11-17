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

#include <algorithm>
#include <chrono>
#include <cstdio>

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
                           const VlcOptions& options, std::string* error, bool synced) {
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
    session->synced = synced;

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

void PlaybackManager::StopSynced() {
    {
        std::lock_guard lock(mu_);
        if (!session_ || !session_->synced) return;
    }
    Stop();
}

PlaybackManager::SyncedReading PlaybackManager::synced_reading() const {
    std::lock_guard lock(mu_);
    SyncedReading r;
    if (!session_ || !session_->synced) return r;
    r.open = true;
    if (!session_->last) return r;
    const VlcStatus& status = *session_->last;
    r.ready = true;
    r.state = status.state;
    r.length = status.length;
    r.rate = status.rate;
    if (const PlaylistItem* item = session_->vlc->Current(status)) r.file_id = item->file_id;
    auto estimate = session_->estimator.At(std::chrono::steady_clock::now());
    r.position = estimate.position;
    r.stalled = estimate.stalled;
    return r;
}

bool PlaybackManager::Control(const std::string& command, double value) {
    std::shared_ptr<Session> session;
    double length = 0;
    {
        std::lock_guard lock(mu_);
        if (!session_ || !session_->synced) return false;
        session = session_;
        if (session->last) length = session->last->length;
    }

    char buf[32];
    std::optional<VlcStatus> answer;
    if (command == "play") {
        answer = session->vlc->Command("pl_forceresume");
    } else if (command == "pause") {
        answer = session->vlc->Command("pl_forcepause");
    } else if (command == "seek") {
        // By fraction of the length, which VLC takes to sub-second precision.
        if (length <= 0) return false;
        std::snprintf(buf, sizeof buf, "%.5f%%", std::clamp(value / length * 100, 0.0, 100.0));
        {
            std::lock_guard lock(mu_);
            session->estimator.Reset();
        }
        answer = session->vlc->Command("seek", buf);
    } else if (command == "rate") {
        std::snprintf(buf, sizeof buf, "%.3f", std::clamp(value, 0.25, 4.0));
        answer = session->vlc->Command("rate", buf);
    } else {
        return false;
    }
    return answer.has_value();
}

std::optional<PlaybackManager::NowPlaying> PlaybackManager::now_playing() const {
    std::lock_guard lock(mu_);
    return now_playing_;
}

void PlaybackManager::Monitor() {
    std::unique_lock lock(mu_);
    while (!stopping_) {
        auto interval = session_ && session_->synced ? std::chrono::milliseconds(kSyncedPoll)
                                                     : std::chrono::milliseconds(1s);
        wake_.wait_for(lock, interval, [this] { return stopping_; });
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
        if (status && session->synced) {
            auto now = std::chrono::steady_clock::now();
            session->last = status;
            session->estimator.Update(status->time, status->state == "playing", status->rate, now);
            // Progress is saved once a second, as for any other session.
            if (now - session->last_track >= 1s) {
                session->last_track = now;
                Track(*session, *status);
            }
        } else if (status) {
            Track(*session, *status);
        }
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
