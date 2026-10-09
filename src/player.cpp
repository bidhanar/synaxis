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

#include "synaxis/process.hpp"

#include <sys/wait.h>

#include <cerrno>
#include <iostream>

namespace synaxis {

bool Player::Play(const std::filesystem::path& path) {
    std::string error;
    pid_t pid = SpawnProcess({"vlc", "--no-one-instance", "--play-and-exit", path.string()},
                             /*detach=*/false, &error);
    if (pid < 0) {
        std::cerr << "error: " << error << "\n";
        return false;
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace synaxis
