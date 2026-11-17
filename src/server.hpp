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

#include "synaxis/config.hpp"
#include "synaxis/fingerprint.hpp"
#include "synaxis/library_service.hpp"
#include "synaxis/playback_manager.hpp"
#include "synaxis/watch_state.hpp"

#include <mutex>
#include <optional>
#include <string>

#include <httplib.h>

namespace synaxis {

// The web UI and its JSON API, served on localhost only.
class Server {
public:
    Server(AppPaths paths, AppConfig config, LibraryService& library, WatchState& state,
           PlaybackManager& playback);

    bool Bind(int port);
    void Run();   // blocks until Stop()
    void Stop();
    void WaitUntilReady();  // until Run() is accepting connections

private:
    // What Play plays: the file and where to start, for a request's
    // profile, title and (optional) file and from_start.
    struct Pick {
        std::optional<Profile> profile;
        const Title* title = nullptr;
        size_t index = 0;  // into title->items
        const VideoFile* file = nullptr;
        double start = 0;
    };
    // Sends the error response itself and returns nullopt on a bad request.
    std::optional<Pick> PickToPlay(const Catalog& catalog, const nlohmann::json& body,
                                   httplib::Response& res);

    void RegisterRoutes();
    AppConfig config() const;

    AppPaths paths_;
    LibraryService& library_;
    WatchState& state_;
    PlaybackManager& playback_;
    Fingerprints fingerprints_;

    mutable std::mutex config_mu_;
    AppConfig config_;

    int port_ = 0;
    httplib::Server http_;
};

} // namespace synaxis
