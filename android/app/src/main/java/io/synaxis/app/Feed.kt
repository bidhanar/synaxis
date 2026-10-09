// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

/** What pressing "Play" on a title does for a profile (port of src/feed.cpp). */
data class UpNext(
    val title: Title,
    val item: Int,             // index into title.items
    val fileId: String,        // which copy to play
    val position: Double,      // seconds to start at
    val duration: Double,      // seconds, 0 if unknown
    val reason: Reason,
    val lastActivity: Long,    // newest progress update on the title; 0 if none
) {
    enum class Reason(val json: String) {
        START("start"),         // never (or barely) watched
        RESUME("resume"),       // stopped partway
        NEXT_EPISODE("next"),   // finished an episode
        REWATCH("rewatch"),     // finished everything
    }
}

private fun latestProgress(profile: Profile, item: TitleItem): Pair<String, Progress>? =
    item.fileIds.mapNotNull { id -> profile.progress[id]?.let { id to it } }.maxByOrNull { it.second.updated }

private fun knownDuration(catalog: Catalog, fileId: String, p: Progress?): Double =
    p?.duration?.takeIf { it > 0 } ?: catalog.findFile(fileId)?.duration ?: 0.0

/** Starting `item` fresh, or resuming it if it was left partway through. */
private fun startOrResume(
    catalog: Catalog,
    profile: Profile,
    title: Title,
    item: Int,
    fresh: UpNext.Reason,
): UpNext {
    val latest = latestProgress(profile, title.items[item])
    if (latest != null) {
        val (id, p) = latest
        if (!p.completed && p.position >= MIN_RESUME_SECONDS) {
            return UpNext(title, item, id, p.position, knownDuration(catalog, id, p), UpNext.Reason.RESUME, 0)
        }
    }
    val id = title.items[item].fileIds.first()
    return UpNext(title, item, id, 0.0, knownDuration(catalog, id, latest?.second), fresh, 0)
}

fun computeUpNext(catalog: Catalog, profile: Profile, title: Title): UpNext {
    var lastItem = -1
    var lastActivity = 0L
    var last: Progress? = null
    title.items.forEachIndexed { i, item ->
        latestProgress(profile, item)?.let { (_, p) ->
            if (p.updated >= lastActivity) {
                lastItem = i
                lastActivity = p.updated
                last = p
            }
        }
    }

    val up = when {
        lastItem < 0 -> startOrResume(catalog, profile, title, 0, UpNext.Reason.START)
        !last!!.completed -> startOrResume(catalog, profile, title, lastItem, UpNext.Reason.START)
        title.kind == TitleKind.SHOW && lastItem + 1 < title.items.size ->
            startOrResume(catalog, profile, title, lastItem + 1, UpNext.Reason.NEXT_EPISODE)
        else -> startOrResume(catalog, profile, title, 0, UpNext.Reason.REWATCH)
            .copy(position = 0.0, reason = UpNext.Reason.REWATCH)
    }
    return up.copy(lastActivity = lastActivity)
}

/** Titles with something to pick up, most recent first, minus dismissed ones. */
fun continueWatching(catalog: Catalog, profile: Profile): List<UpNext> =
    catalog.titles.asSequence()
        .map { computeUpNext(catalog, profile, it) }
        .filter { it.lastActivity > 0 }
        .filter { it.reason == UpNext.Reason.RESUME || it.reason == UpNext.Reason.NEXT_EPISODE }
        .filter { (profile.dismissed[it.title.id] ?: -1) < it.lastActivity }
        .sortedByDescending { it.lastActivity }
        .toList()
