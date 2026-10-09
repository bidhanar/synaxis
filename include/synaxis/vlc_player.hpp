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

#include <sys/types.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace synaxis {

// What VLC's HTTP interface reports in /requests/status.json.
struct VlcStatus {
    std::string state;     // "playing", "paused" or "stopped"
    std::string filename;  // basename of the current item, if known
    double time = 0;       // seconds into the current item
    double length = 0;     // seconds; 0 if unknown

    bool operator==(const VlcStatus&) const = default;
};

std::optional<VlcStatus> ParseVlcStatus(const std::string& body);

struct PlaylistItem {
    std::string file_id;
    std::filesystem::path path;
    std::string title;  // shown in VLC's title bar
    double start = 0;   // seconds; only honoured for the first item
};

struct VlcOptions {
    std::string executable = "vlc";
    bool fullscreen = true;
};

// The command line that plays `playlist` in a fresh VLC window, with its
// HTTP control interface on localhost:`port` so playback can be followed.
std::vector<std::string> VlcCommandLine(const VlcOptions& options,
                                        const std::vector<PlaylistItem>& playlist, int port,
                                        const std::string& password);

// A VLC process we launched.
class VlcSession {
public:
    static std::unique_ptr<VlcSession> Launch(const VlcOptions& options,
                                              std::vector<PlaylistItem> playlist,
                                              std::string* error);
    ~VlcSession();

    // False once VLC has exited (and been reaped).
    bool Running();
    // Asks VLC what it's doing; nullopt if it isn't answering (yet).
    std::optional<VlcStatus> Poll() const;
    // The playlist entry VLC says is current.
    const PlaylistItem* Current(const VlcStatus& status) const;
    // Asks VLC to quit and waits briefly for it.
    void Terminate();

    const std::vector<PlaylistItem>& playlist() const { return playlist_; }

private:
    VlcSession() = default;

    pid_t pid_ = -1;
    std::atomic<bool> exited_ = false;  // Running() is called from two threads
    int port_ = 0;
    std::string password_;
    std::vector<PlaylistItem> playlist_;
};

} // namespace synaxis
