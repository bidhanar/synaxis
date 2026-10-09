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

// Linked after the bundled FFmpeg (see CMakeLists.txt) to keep packages
// installable on older distributions.
//
// glibc 2.43 gave atan2f a new symbol version, so a binary built on a 2.43+
// system that calls it won't load on older ones (Ubuntu 24.04 has 2.39).
// The link wraps FFmpeg's calls (-Wl,--wrap=atan2f) to land here instead,
// on the double-precision atan2 that every glibc provides.

#include <cmath>

extern "C" float __wrap_atan2f(float y, float x) {
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}
