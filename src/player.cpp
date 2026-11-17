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

#include "synaxis/player.hpp"

#include "synaxis/platform.hpp"
#include "synaxis/process.hpp"
#include "synaxis/vlc_player.hpp"

#include <iostream>

namespace synaxis {

bool Player::Play(const std::filesystem::path& path) {
    std::string error;
    auto process = Process::Spawn(
        {FindVlc("vlc"), "--no-one-instance", "--play-and-exit", PathToUtf8(path)},
        /*detached=*/false, &error);
    if (!process) {
        std::cerr << "error: " << error << "\n";
        return false;
    }
    return process->Wait() == 0;
}

} // namespace synaxis
