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

#include <chrono>
#include <optional>

namespace synaxis {

// VLC only updates the position it reports every quarter to half a second,
// so a single reading can be half a second old. This turns those steps into
// a smooth, current position: each time the reported value changes we know
// the position at (about) that moment, and we count forward from there.
// (Ported from Tandem's local/src/positionEstimator.ts.)
class PositionEstimator {
public:
    using Clock = std::chrono::steady_clock;

    // If VLC says "playing" but the value hasn't moved for this long, it's stuck.
    static constexpr std::chrono::milliseconds kStall{1200};

    // `poll` is how often Update() is called.
    explicit PositionEstimator(std::chrono::milliseconds poll) : poll_(poll) {}

    void Reset() { last_raw_.reset(); }

    // A new reading from VLC: `raw` seconds, playing or not, at `now`.
    void Update(double raw, bool playing, double rate, Clock::time_point now) {
        if (!last_raw_ || raw != *last_raw_) {
            last_raw_ = raw;
            anchor_value_ = raw;
            // The value changed some time since the last poll: on average, half a poll ago.
            anchor_at_ = now - poll_ / 2;
        }
        playing_ = playing;
        rate_ = rate;
        if (!playing) anchor_at_ = now;  // paused: the value is simply where we are
    }

    struct Estimate {
        double position = 0;
        bool stalled = false;
    };
    // Where playback is at `now` (normally a little after the last Update).
    Estimate At(Clock::time_point now) const {
        if (!last_raw_) return {};
        if (!playing_) return {anchor_value_, false};
        auto since = now - anchor_at_;
        if (since > kStall) return {anchor_value_, true};
        return {anchor_value_ + std::chrono::duration<double>(since).count() * rate_, false};
    }

private:
    std::chrono::milliseconds poll_;
    std::optional<double> last_raw_;
    double anchor_value_ = 0;
    Clock::time_point anchor_at_{};
    bool playing_ = false;
    double rate_ = 1;
};

} // namespace synaxis
