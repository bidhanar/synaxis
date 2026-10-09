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

#include "synaxis/process.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace synaxis {

namespace {

using nlohmann::json;

// Asks the kernel for an unused localhost port. There's a small window
// before VLC binds it, which is fine for a desktop app.
int FreeLocalPort() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    socklen_t len = sizeof addr;
    int port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 &&
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
    close(fd);
    return port;
}

std::string RandomPassword() {
    std::random_device rd;
    char buf[17];
    std::snprintf(buf, sizeof buf, "%08x%08x", rd(), rd());
    return buf;
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
        argv.push_back(item.path.string());
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

    session->pid_ = SpawnProcess(
        VlcCommandLine(options, session->playlist_, session->port_, session->password_),
        /*detach=*/true, error);
    if (session->pid_ < 0) return nullptr;
    return session;
}

VlcSession::~VlcSession() {
    Running();  // reap if it already exited, so it doesn't linger as a zombie
}

bool VlcSession::Running() {
    if (exited_ || pid_ < 0) return false;
    int status = 0;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r == pid_ || (r < 0 && errno == ECHILD)) exited_ = true;
    return !exited_;
}

std::optional<VlcStatus> VlcSession::Poll() const {
    httplib::Client client("127.0.0.1", port_);
    client.set_connection_timeout(0, 500'000);
    client.set_read_timeout(1, 0);
    client.set_basic_auth("", password_);
    auto res = client.Get("/requests/status.json");
    if (!res || res->status != 200) return std::nullopt;
    return ParseVlcStatus(res->body);
}

const PlaylistItem* VlcSession::Current(const VlcStatus& status) const {
    if (status.filename.empty()) return playlist_.size() == 1 ? &playlist_.front() : nullptr;
    for (const PlaylistItem& item : playlist_) {
        if (item.path.filename().string() == status.filename) return &item;
    }
    return nullptr;
}

void VlcSession::Terminate() {
    if (!Running()) return;
    kill(pid_, SIGTERM);
    for (int i = 0; i < 20 && Running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace synaxis
