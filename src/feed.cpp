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

#include "synaxis/feed.hpp"

#include <algorithm>

namespace synaxis {

namespace {

struct Latest {
    const Progress* progress = nullptr;
    std::string file_id;
};

// The most recently touched copy of an item, if any copy was ever played.
Latest LatestProgress(const Profile& profile, const TitleItem& item) {
    Latest latest;
    for (const std::string& id : item.file_ids) {
        auto it = profile.progress.find(id);
        if (it == profile.progress.end()) continue;
        if (!latest.progress || it->second.updated > latest.progress->updated) {
            latest = {&it->second, id};
        }
    }
    return latest;
}

double KnownDuration(const Catalog& catalog, const std::string& file_id, const Progress* p) {
    if (p && p->duration > 0) return p->duration;
    const VideoFile* f = catalog.FindFile(file_id);
    return f && f->info ? f->info->duration : 0;
}

// Starting `item` fresh, or resuming it if it was left partway through.
UpNext StartOrResume(const Catalog& catalog, const Profile& profile, const Title& title,
                     size_t item, UpNext::Reason fresh_reason) {
    UpNext up;
    up.title = &title;
    up.item = item;
    up.reason = fresh_reason;
    up.file_id = title.items[item].file_ids.front();

    Latest latest = LatestProgress(profile, title.items[item]);
    if (latest.progress && !latest.progress->completed &&
        latest.progress->position >= kMinResumeSeconds) {
        up.file_id = latest.file_id;
        up.position = latest.progress->position;
        up.reason = UpNext::Reason::kResume;
    }
    up.duration = KnownDuration(catalog, up.file_id, latest.progress);
    return up;
}

} // namespace

UpNext ComputeUpNext(const Catalog& catalog, const Profile& profile, const Title& title) {
    // Find the item touched most recently.
    std::optional<size_t> last_item;
    std::int64_t last_activity = 0;
    const Progress* last = nullptr;
    for (size_t i = 0; i < title.items.size(); ++i) {
        Latest latest = LatestProgress(profile, title.items[i]);
        if (latest.progress && latest.progress->updated >= last_activity) {
            last_item = i;
            last_activity = latest.progress->updated;
            last = latest.progress;
        }
    }

    UpNext up;
    if (!last_item) {
        up = StartOrResume(catalog, profile, title, 0, UpNext::Reason::kStart);
    } else if (!last->completed) {
        up = StartOrResume(catalog, profile, title, *last_item, UpNext::Reason::kStart);
    } else if (title.kind == TitleKind::kShow && *last_item + 1 < title.items.size()) {
        up = StartOrResume(catalog, profile, title, *last_item + 1, UpNext::Reason::kNextEpisode);
    } else {
        up = StartOrResume(catalog, profile, title, 0, UpNext::Reason::kRewatch);
        up.position = 0;
        up.reason = UpNext::Reason::kRewatch;
    }
    up.last_activity = last_activity;
    return up;
}

std::vector<UpNext> ContinueWatching(const Catalog& catalog, const Profile& profile) {
    std::vector<UpNext> result;
    for (const Title& title : catalog.titles()) {
        UpNext up = ComputeUpNext(catalog, profile, title);
        if (up.last_activity == 0) continue;
        if (up.reason != UpNext::Reason::kResume && up.reason != UpNext::Reason::kNextEpisode) {
            continue;
        }
        auto dismissed = profile.dismissed.find(title.id);
        if (dismissed != profile.dismissed.end() && dismissed->second >= up.last_activity) continue;
        result.push_back(up);
    }
    std::stable_sort(result.begin(), result.end(), [](const UpNext& a, const UpNext& b) {
        return a.last_activity > b.last_activity;
    });
    return result;
}

} // namespace synaxis
