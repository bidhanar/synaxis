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

#include "synaxis/vlc_player.hpp"

#include "synaxis/platform.hpp"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace synaxis {

namespace {

using nlohmann::json;

std::string RandomPassword() {
    std::random_device rd;
    char buf[17];
    std::snprintf(buf, sizeof buf, "%08x%08x", rd(), rd());
    return buf;
}

// Percent-encodes everything but letters, digits and "-._~".
std::string UrlEncode(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += char(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

} // namespace

std::optional<VlcStatus> ParseVlcStatus(const std::string& body) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("state")) return std::nullopt;

    VlcStatus status;
    try {
        status.state = j.at("state").get<std::string>();
        status.length = j.value("length", 0.0);
        status.time = j.value("time", 0.0);
        status.rate = j.value("rate", 1.0);
        // `time` is whole seconds; position * length is finer-grained.
        if (status.length > 0 && j.contains("position") && j.at("position").is_number()) {
            status.time = j.at("position").get<double>() * status.length;
        }
        const json* meta = &j;
        for (const char* key : {"information", "category", "meta"}) {
            meta = meta->is_object() && meta->contains(key) ? &meta->at(key) : nullptr;
            if (!meta) break;
        }
        if (meta && meta->contains("filename") && meta->at("filename").is_string()) {
            status.filename = meta->at("filename").get<std::string>();
        }
    } catch (const json::exception&) {
        return std::nullopt;
    }
    return status;
}

std::string FindVlc(const std::string& configured) {
#ifdef _WIN32
    if (configured != "vlc") return configured;
    for (const char* variable : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        auto root = GetEnv(variable);
        if (!root) continue;
        std::filesystem::path exe = PathFromUtf8(*root) / "VideoLAN" / "VLC" / "vlc.exe";
        std::error_code ec;
        if (std::filesystem::exists(exe, ec)) return PathToUtf8(exe);
    }
#endif
    return configured;
}

std::vector<std::string> VlcCommandLine(const VlcOptions& options,
                                        const std::vector<PlaylistItem>& playlist, int port,
                                        const std::string& password) {
    std::vector<std::string> argv = {
        options.executable,
        // A separate instance, even if the user's VLC is set to reuse one:
        // otherwise the files get handed off and our control port never opens.
        "--no-one-instance",
        "--no-playlist-enqueue",
        "--extraintf=http",
        "--http-host=127.0.0.1",
        "--http-port=" + std::to_string(port),
        "--http-password=" + password,
        // We handle resuming; VLC's own "continue from...?" prompt would
        // fight with it.
        "--qt-continue=0",
        "--play-and-exit",
        "--no-loop",
        "--no-repeat",
        "--no-random",
    };
    if (options.fullscreen) argv.push_back("--fullscreen");

    for (size_t i = 0; i < playlist.size(); ++i) {
        const PlaylistItem& item = playlist[i];
        argv.push_back(PathToUtf8(item.path));
        // ":option" after a file applies to that playlist item only.
        if (i == 0 && item.start > 0) {
            char buf[32];
            std::snprintf(buf, sizeof buf, ":start-time=%.1f", item.start);
            argv.push_back(buf);
        }
        if (!item.title.empty()) argv.push_back(":meta-title=" + item.title);
    }
    return argv;
}

std::unique_ptr<VlcSession> VlcSession::Launch(const VlcOptions& options,
                                               std::vector<PlaylistItem> playlist,
                                               std::string* error) {
    if (playlist.empty()) {
        if (error) *error = "nothing to play";
        return nullptr;
    }
    std::unique_ptr<VlcSession> session(new VlcSession());
    session->port_ = FreeLocalPort();
    if (session->port_ == 0) {
        if (error) *error = "no free port for VLC's control interface";
        return nullptr;
    }
    session->password_ = RandomPassword();
    session->playlist_ = std::move(playlist);

    VlcOptions resolved = options;
    resolved.executable = FindVlc(options.executable);
    session->process_ = Process::Spawn(
        VlcCommandLine(resolved, session->playlist_, session->port_, session->password_),
        /*detached=*/true, error);
    if (!session->process_) {
        if (error) *error = "Couldn't start VLC (" + *error + "). Is it installed? Check the VLC program in Settings.";
        return nullptr;
    }
    return session;
}

bool VlcSession::Running() {
    return process_->Running();
}

std::optional<VlcStatus> VlcSession::Poll() const {
    return Command("");
}

std::optional<VlcStatus> VlcSession::Command(const std::string& command,
                                             const std::string& value) const {
    if (!process_->Running()) return std::nullopt;
    httplib::Client client("127.0.0.1", port_);
    client.set_connection_timeout(0, 500'000);
    client.set_read_timeout(1, 0);
    client.set_basic_auth("", password_);
    std::string path = "/requests/status.json";
    if (!command.empty()) {
        path += "?command=" + UrlEncode(command);
        if (!value.empty()) path += "&val=" + UrlEncode(value);
    }
    auto res = client.Get(path);
    if (!res || res->status != 200) return std::nullopt;
    return ParseVlcStatus(res->body);
}

const PlaylistItem* VlcSession::Current(const VlcStatus& status) const {
    if (status.filename.empty()) return playlist_.size() == 1 ? &playlist_.front() : nullptr;
    for (const PlaylistItem& item : playlist_) {
        if (PathToUtf8(item.path.filename()) == status.filename) return &item;
    }
    return nullptr;
}

void VlcSession::Terminate() {
    process_->Terminate();
}

} // namespace synaxis
