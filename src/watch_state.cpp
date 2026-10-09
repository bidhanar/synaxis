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

#include "synaxis/watch_state.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>

#include <nlohmann/json.hpp>

namespace synaxis {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// Position reports arrive every second or so; don't rewrite the file that often.
constexpr std::int64_t kSaveIntervalSeconds = 5;

std::string RandomId() {
    std::random_device rd;
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", rd());
    return buf;
}

json ToJson(const Profile& p) {
    json progress = json::object();
    for (const auto& [id, pr] : p.progress) {
        progress[id] = {{"position", pr.position},
                        {"duration", pr.duration},
                        {"updated", pr.updated},
                        {"completed", pr.completed},
                        {"plays", pr.plays}};
    }
    return {{"id", p.id},
            {"name", p.name},
            {"avatar", p.avatar},
            {"progress", progress},
            {"my_list", p.my_list},
            {"dismissed", p.dismissed}};
}

Profile ProfileFromJson(const json& j) {
    Profile p;
    p.id = j.at("id").get<std::string>();
    p.name = j.at("name").get<std::string>();
    p.avatar = j.value("avatar", 0);
    const json progress = j.value("progress", json::object());  // must outlive items()
    for (const auto& [id, pr] : progress.items()) {
        p.progress[id] = Progress{pr.value("position", 0.0), pr.value("duration", 0.0),
                                  pr.value("updated", std::int64_t{0}),
                                  pr.value("completed", false), pr.value("plays", 0)};
    }
    p.my_list = j.value("my_list", std::map<std::string, std::int64_t>{});
    p.dismissed = j.value("dismissed", std::map<std::string, std::int64_t>{});
    return p;
}

} // namespace

bool IsFinished(double position, double duration) {
    if (duration <= 0) return false;
    return duration - position <= std::max(30.0, duration * 0.05);
}

WatchState::WatchState(fs::path path) : path_(std::move(path)) {
    std::ifstream in(path_);
    if (!in) return;
    json root = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) return;
    try {
        for (const json& p : root.value("profiles", json::array())) {
            profiles_.push_back(ProfileFromJson(p));
        }
    } catch (const json::exception&) {
        // Keep whatever loaded cleanly rather than losing everything.
    }
}

WatchState::~WatchState() {
    Flush();
}

std::vector<Profile> WatchState::profiles() const {
    std::lock_guard lock(mu_);
    return profiles_;
}

std::optional<Profile> WatchState::profile(const std::string& id) const {
    std::lock_guard lock(mu_);
    for (const Profile& p : profiles_) {
        if (p.id == id) return p;
    }
    return std::nullopt;
}

Profile WatchState::AddProfile(const std::string& name, int avatar) {
    std::lock_guard lock(mu_);
    Profile p;
    do {
        p.id = RandomId();
    } while (FindLocked(p.id));
    p.name = name;
    p.avatar = avatar;
    profiles_.push_back(p);
    SaveLocked();
    return p;
}

bool WatchState::UpdateProfile(const std::string& id, const std::string& name, int avatar) {
    std::lock_guard lock(mu_);
    Profile* p = FindLocked(id);
    if (!p) return false;
    p->name = name;
    p->avatar = avatar;
    SaveLocked();
    return true;
}

bool WatchState::RemoveProfile(const std::string& id) {
    std::lock_guard lock(mu_);
    auto it = std::find_if(profiles_.begin(), profiles_.end(),
                           [&](const Profile& p) { return p.id == id; });
    if (it == profiles_.end()) return false;
    profiles_.erase(it);
    SaveLocked();
    return true;
}

void WatchState::RecordStart(const std::string& profile, const std::string& file_id,
                             std::int64_t now) {
    std::lock_guard lock(mu_);
    Profile* p = FindLocked(profile);
    if (!p) return;
    Progress& pr = p->progress[file_id];
    pr.plays++;
    pr.updated = now;
    SaveLocked();
}

void WatchState::RecordPosition(const std::string& profile, const std::string& file_id,
                                double position, double duration, std::int64_t now) {
    std::lock_guard lock(mu_);
    Profile* p = FindLocked(profile);
    if (!p) return;
    Progress& pr = p->progress[file_id];
    bool was_completed = pr.completed;
    pr.position = position;
    if (duration > 0) pr.duration = duration;
    pr.completed = IsFinished(position, pr.duration);
    pr.updated = now;
    dirty_ = true;
    if (pr.completed != was_completed || now - last_save_ >= kSaveIntervalSeconds) {
        SaveLocked();
        last_save_ = now;
    }
}

void WatchState::SetWatched(const std::string& profile, const std::vector<std::string>& file_ids,
                            bool watched, std::int64_t now) {
    std::lock_guard lock(mu_);
    Profile* p = FindLocked(profile);
    if (!p) return;
    for (const std::string& id : file_ids) {
        if (!watched) {
            p->progress.erase(id);
            continue;
        }
        Progress& pr = p->progress[id];
        pr.position = pr.duration;
        pr.completed = true;
        pr.updated = now;
    }
    SaveLocked();
}

bool WatchState::ToggleMyList(const std::string& profile, const std::string& title_id,
                              std::int64_t now) {
    std::lock_guard lock(mu_);
    Profile* p = FindLocked(profile);
    if (!p) return false;
    bool added = p->my_list.erase(title_id) == 0;
    if (added) p->my_list[title_id] = now;
    SaveLocked();
    return added;
}

void WatchState::Dismiss(const std::string& profile, const std::string& title_id,
                         std::int64_t now) {
    std::lock_guard lock(mu_);
    Profile* p = FindLocked(profile);
    if (!p) return;
    p->dismissed[title_id] = now;
    SaveLocked();
}

void WatchState::Flush() {
    std::lock_guard lock(mu_);
    if (dirty_) SaveLocked();
}

Profile* WatchState::FindLocked(const std::string& id) {
    for (Profile& p : profiles_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void WatchState::SaveLocked() {
    json profiles = json::array();
    for (const Profile& p : profiles_) profiles.push_back(ToJson(p));

    // Write-then-rename so a crash mid-write can't corrupt the history.
    fs::path tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << json{{"version", 1}, {"profiles", profiles}}.dump(1);
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, path_, ec);
    if (!ec) dirty_ = false;
}

} // namespace synaxis
