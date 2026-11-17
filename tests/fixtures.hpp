// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <filesystem>

#include "synaxis/platform.hpp"

// tests/fixtures: where the build found it, unless SYNAXIS_FIXTURES_DIR says
// otherwise (CI runs the Windows test programs on another machine).
inline std::filesystem::path FixturesDir() {
    if (auto dir = synaxis::GetEnv("SYNAXIS_FIXTURES_DIR")) return synaxis::PathFromUtf8(*dir);
    return std::filesystem::path(FIXTURES_DIR);
}
