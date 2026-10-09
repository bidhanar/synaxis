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

#include "synaxis/catalog.hpp"
#include "synaxis/watch_state.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace synaxis {

// What pressing "Play" on a title should do for a given profile.
struct UpNext {
    enum class Reason {
        kStart,        // never watched (or barely): start at the beginning
        kResume,       // stopped partway: pick up where they left off
        kNextEpisode,  // finished an episode: on to the next one
        kRewatch,      // finished everything: back to the start
    };

    const Title* title = nullptr;
    size_t item = 0;           // index into title->items
    std::string file_id;       // which copy to play
    double position = 0;       // seconds to start at
    double duration = 0;       // seconds, 0 if unknown
    Reason reason = Reason::kStart;
    std::int64_t last_activity = 0;  // newest progress update on the title; 0 if none
};

UpNext ComputeUpNext(const Catalog& catalog, const Profile& profile, const Title& title);

// Titles with something to pick up (a resume point or the next episode),
// most recently watched first, minus ones dismissed since their last play.
std::vector<UpNext> ContinueWatching(const Catalog& catalog, const Profile& profile);

} // namespace synaxis
