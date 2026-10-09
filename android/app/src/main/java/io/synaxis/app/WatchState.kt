// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import kotlin.math.max
import kotlin.random.Random

/** How far a profile got through one file. */
data class Progress(
    val position: Double = 0.0,  // seconds
    val duration: Double = 0.0,  // seconds; 0 if unknown
    val updated: Long = 0,       // unix seconds of the last change
    val completed: Boolean = false,
    val plays: Int = 0,
)

/** Within the last 5% (end credits) or the last 30 seconds counts as watched. */
fun isFinished(position: Double, duration: Double): Boolean =
    duration > 0 && duration - position <= max(30.0, duration * 0.05)

/** Resume points below this are treated as "not really started". */
const val MIN_RESUME_SECONDS = 10.0

data class Profile(
    val id: String,
    val name: String,
    val avatar: Int,
    val progress: Map<String, Progress> = emptyMap(),  // by file id
    val myList: Map<String, Long> = emptyMap(),        // title id -> time added
    val dismissed: Map<String, Long> = emptyMap(),     // title id -> time hidden from Continue Watching
)

/**
 * Per-profile viewing history, saved as JSON in the same format as the
 * desktop app. Thread-safe: profiles are immutable snapshots.
 */
class WatchState(private val file: File) {
    private val lock = Any()
    private var profiles: List<Profile> = load()

    fun profiles(): List<Profile> = synchronized(lock) { profiles }
    fun profile(id: String): Profile? = synchronized(lock) { profiles.find { it.id == id } }

    fun addProfile(name: String, avatar: Int): Profile = synchronized(lock) {
        var id: String
        do {
            id = "%08x".format(Random.nextInt())
        } while (profiles.any { it.id == id })
        val p = Profile(id, name, avatar)
        profiles = profiles + p
        save()
        p
    }

    fun updateProfile(id: String, name: String, avatar: Int): Boolean =
        change(id) { it.copy(name = name, avatar = avatar) }

    fun removeProfile(id: String): Boolean = synchronized(lock) {
        val before = profiles.size
        profiles = profiles.filter { it.id != id }
        if (profiles.size != before) save()
        profiles.size != before
    }

    fun recordStart(profile: String, fileId: String, now: Long) {
        change(profile) { p ->
            val old = p.progress[fileId] ?: Progress()
            p.copy(progress = p.progress + (fileId to old.copy(plays = old.plays + 1, updated = now)))
        }
    }

    fun recordPosition(profile: String, fileId: String, position: Double, duration: Double, now: Long) {
        change(profile) { p ->
            val old = p.progress[fileId] ?: Progress()
            val dur = if (duration > 0) duration else old.duration
            val next = old.copy(
                position = position,
                duration = dur,
                completed = isFinished(position, dur),
                updated = now,
            )
            p.copy(progress = p.progress + (fileId to next))
        }
    }

    /** Watched: the position jumps to the end. Unwatched: the history is cleared. */
    fun setWatched(profile: String, fileIds: List<String>, watched: Boolean, now: Long) {
        change(profile) { p ->
            val progress = p.progress.toMutableMap()
            for (id in fileIds) {
                if (!watched) {
                    progress.remove(id)
                } else {
                    val old = progress[id] ?: Progress()
                    progress[id] = old.copy(position = old.duration, completed = true, updated = now)
                }
            }
            p.copy(progress = progress)
        }
    }

    /** Returns whether the title is in the list afterwards. */
    fun toggleMyList(profile: String, titleId: String, now: Long): Boolean {
        var added = false
        change(profile) { p ->
            added = titleId !in p.myList
            p.copy(myList = if (added) p.myList + (titleId to now) else p.myList - titleId)
        }
        return added
    }

    fun dismiss(profile: String, titleId: String, now: Long) {
        change(profile) { it.copy(dismissed = it.dismissed + (titleId to now)) }
    }

    private fun change(id: String, update: (Profile) -> Profile): Boolean = synchronized(lock) {
        val index = profiles.indexOfFirst { it.id == id }
        if (index < 0) return false
        profiles = profiles.toMutableList().also { it[index] = update(it[index]) }
        save()
        true
    }

    private fun load(): List<Profile> {
        if (!file.exists()) return emptyList()
        return try {
            val root = JSONObject(file.readText())
            val arr = root.optJSONArray("profiles") ?: return emptyList()
            (0 until arr.length()).map { profileFromJson(arr.getJSONObject(it)) }
        } catch (e: Exception) {
            emptyList()
        }
    }

    // Write-then-rename, so a crash mid-write can't corrupt the history.
    private fun save() {
        val arr = JSONArray()
        profiles.forEach { arr.put(toJson(it)) }
        val tmp = File(file.path + ".tmp")
        tmp.writeText(JSONObject().put("version", 1).put("profiles", arr).toString(1))
        tmp.renameTo(file)
    }

    private fun toJson(p: Profile): JSONObject {
        val progress = JSONObject()
        p.progress.forEach { (id, pr) ->
            progress.put(
                id,
                JSONObject()
                    .put("position", pr.position)
                    .put("duration", pr.duration)
                    .put("updated", pr.updated)
                    .put("completed", pr.completed)
                    .put("plays", pr.plays),
            )
        }
        return JSONObject()
            .put("id", p.id)
            .put("name", p.name)
            .put("avatar", p.avatar)
            .put("progress", progress)
            .put("my_list", JSONObject(p.myList))
            .put("dismissed", JSONObject(p.dismissed))
    }

    private fun profileFromJson(j: JSONObject): Profile {
        fun longMap(key: String): Map<String, Long> {
            val o = j.optJSONObject(key) ?: return emptyMap()
            return o.keys().asSequence().associateWith { o.getLong(it) }
        }
        val progress = mutableMapOf<String, Progress>()
        j.optJSONObject("progress")?.let { o ->
            for (id in o.keys()) {
                val pr = o.getJSONObject(id)
                progress[id] = Progress(
                    pr.optDouble("position", 0.0),
                    pr.optDouble("duration", 0.0),
                    pr.optLong("updated", 0),
                    pr.optBoolean("completed", false),
                    pr.optInt("plays", 0),
                )
            }
        }
        return Profile(
            id = j.getString("id"),
            name = j.getString("name"),
            avatar = j.optInt("avatar", 0),
            progress = progress,
            myList = longMap("my_list"),
            dismissed = longMap("dismissed"),
        )
    }
}
