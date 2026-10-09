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

#include <cstddef>

namespace synaxis::web {

// A file from web/, compiled in by cmake/EmbedAssets.cmake.
struct Asset {
    const char* path;  // URL path, e.g. "/app.js"
    const char* mime;
    const unsigned char* data;
    std::size_t size;
};

extern const Asset kAssets[];
extern const std::size_t kAssetCount;

} // namespace synaxis::web
