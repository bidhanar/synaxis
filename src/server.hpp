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
#include "synaxis/library_service.hpp"
#include "synaxis/playback_manager.hpp"
#include "synaxis/watch_state.hpp"

#include <mutex>
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
    void RegisterRoutes();
    AppConfig config() const;

    AppPaths paths_;
    LibraryService& library_;
    WatchState& state_;
    PlaybackManager& playback_;

    mutable std::mutex config_mu_;
    AppConfig config_;

    int port_ = 0;
    httplib::Server http_;
};

} // namespace synaxis
