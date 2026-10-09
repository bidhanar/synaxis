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

#include "synaxis/process.hpp"

#include <fcntl.h>
#include <spawn.h>

#include <cstring>

extern char** environ;

namespace synaxis {

pid_t SpawnProcess(const std::vector<std::string>& argv, bool detach, std::string* error) {
    if (argv.empty()) {
        if (error) *error = "empty command";
        return -1;
    }

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    if (detach) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, args[0], &actions, &attr, args.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);

    if (rc != 0) {
        if (error) *error = "could not start " + argv[0] + ": " + std::strerror(rc);
        return -1;
    }
    return pid;
}

} // namespace synaxis
