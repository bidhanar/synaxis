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

#include <string>
#include <vector>

namespace synaxis {

// Starts `argv` (looked up on PATH) with stdio on /dev/null and no other
// inherited file descriptors, so the child can't keep our listening socket
// open after we exit. With `detach`, the child gets its own session and
// won't receive Ctrl-C aimed at our terminal. Returns the pid, or -1 with
// `error` set.
pid_t SpawnProcess(const std::vector<std::string>& argv, bool detach, std::string* error);

} // namespace synaxis
