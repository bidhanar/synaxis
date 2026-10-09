// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

import org.json.JSONArray
import org.json.JSONObject
import java.net.URLDecoder
import kotlin.random.Random

/** What to hand the video player. */
data class PlayRequest(
    val profileId: String,
    val titleId: String,
    val fileId: String,
    val uri: String,
    val label: String,   // shown in the player
    val start: Double,   // seconds
) {
    fun toJson(): String = JSONObject()
        .put("profile", profileId).put("title", titleId).put("file", fileId)
        .put("uri", uri).put("label", label).put("start", start).toString()

    companion object {
        fun fromJson(s: String): PlayRequest = JSONObject(s).let {
            PlayRequest(
                it.getString("profile"), it.getString("title"), it.getString("file"),
                it.getString("uri"), it.getString("label"), it.getDouble("start"),
            )
        }
    }
}

/**
 * The JSON API behind the web UI: the same routes and shapes as the desktop
 * server (src/server.cpp), answered in-process through [Bridge].
 */
class Api(
    private val library: Library,
    private val state: WatchState,
    private val settings: SettingsStore,
    private val host: Host,
    private val version: String,
) {
    interface Host {
        fun hasPermission(): Boolean
        fun requestPermission()
        fun play(request: PlayRequest)
    }

    private class HttpError(val status: Int, message: String) : Exception(message)

    /** Returns `{"status": <code>, "body": <json>}`. */
    fun handle(method: String, url: String, body: String): String {
        val (status, payload) = try {
            200 to route(method, url, body)
        } catch (e: HttpError) {
            e.status to JSONObject().put("error", e.message)
        } catch (e: Exception) {
            500 to JSONObject().put("error", e.message ?: e.javaClass.simpleName)
        }
        return JSONObject().put("status", status).put("body", payload).toString()
    }

    private fun route(method: String, url: String, rawBody: String): Any {
        val path = url.substringBefore('?')
        val query = url.substringAfter('?', "").split('&').filter { it.isNotEmpty() }.associate {
            URLDecoder.decode(it.substringBefore('='), "UTF-8") to
                URLDecoder.decode(it.substringAfter('=', ""), "UTF-8")
        }
        val body = if (rawBody.isBlank()) JSONObject() else JSONObject(rawBody)
        val segments = path.trim('/').split('/')

        return when {
            method == "GET" && path == "/api/state" -> stateJson()
            method == "GET" && path == "/api/status" -> statusJson()
            method == "POST" && path == "/api/profiles" -> {
                val name = body.optString("name").trim().ifEmpty { throw HttpError(400, "a profile needs a name") }
                profileJson(state.addProfile(name, body.optInt("avatar", 0)))
            }
            method == "POST" && segments.size == 3 && segments[1] == "profiles" -> {
                val name = body.optString("name").trim().ifEmpty { throw HttpError(400, "a profile needs a name") }
                if (!state.updateProfile(segments[2], name, body.optInt("avatar", 0))) throw HttpError(404, "no such profile")
                ok()
            }
            method == "DELETE" && segments.size == 3 && segments[1] == "profiles" -> {
                if (!state.removeProfile(segments[2])) throw HttpError(404, "no such profile")
                ok()
            }
            method == "GET" && path == "/api/home" -> view(query["profile"]).home(query["filter"] ?: "all", state.profiles())
            method == "GET" && path == "/api/my-list" -> view(query["profile"]).myList()
            method == "GET" && path == "/api/search" -> view(query["profile"]).search(query["q"] ?: "")
            method == "GET" && segments.size == 3 && segments[1] == "titles" -> view(query["profile"]).detail(segments[2])
            method == "POST" && path == "/api/play" -> play(body)
            method == "POST" && path == "/api/my-list" -> {
                val profile = requireProfile(body.optString("profile"))
                JSONObject().put("in_list", state.toggleMyList(profile.id, body.optString("title"), Library.now()))
            }
            method == "POST" && path == "/api/watched" -> watched(body)
            method == "POST" && path == "/api/dismiss" -> {
                val profile = requireProfile(body.optString("profile"))
                state.dismiss(profile.id, body.optString("title"), Library.now())
                ok()
            }
            method == "POST" && path == "/api/settings" -> saveSettings(body)
            method == "POST" && path == "/api/rescan" -> {
                library.retryThumbnails()
                library.rescan(settings.get(), host.hasPermission())
                ok()
            }
            method == "POST" && path == "/api/permission" -> {
                host.requestPermission()
                ok()
            }
            else -> throw HttpError(404, "no route for $method $path")
        }
    }

    // ---- Playback ----

    private fun play(body: JSONObject): JSONObject {
        val profile = requireProfile(body.optString("profile"))
        val catalog = library.catalog
        val title = catalog.findTitle(body.optString("title")) ?: throw HttpError(404, "no such title")

        var fileId = body.optString("file")
        var start: Double
        val index: Int
        if (fileId.isNotEmpty()) {
            val (owner, i) = catalog.locate(fileId) ?: throw HttpError(404, "no such file")
            if (owner.id != title.id) throw HttpError(404, "no such file")
            index = i
            val p = profile.progress[fileId]
            start = if (p != null && !p.completed && p.position >= MIN_RESUME_SECONDS) p.position else 0.0
        } else {
            val up = computeUpNext(catalog, profile, title)
            index = up.item
            fileId = up.fileId
            start = up.position
        }
        if (body.optBoolean("from_start", false)) start = 0.0

        val file = catalog.findFile(fileId) ?: throw HttpError(410, "That video isn't on this phone anymore.")
        host.play(PlayRequest(profile.id, title.id, file.id, file.uri, playerLabel(catalog, title, index), start))
        return JSONObject().put("ok", true).put("start", start)
    }

    /**
     * Called when the player returns: records where it stopped and, after a
     * finished episode, describes the next one so the UI can offer autoplay.
     */
    fun playbackEnded(request: PlayRequest, position: Double?, duration: Double?): JSONObject {
        val catalog = library.catalog
        val file = catalog.findFile(request.fileId)
        val knownDuration = duration?.takeIf { it > 0 } ?: file?.duration ?: 0.0
        if (position != null && position >= 0) {
            state.recordPosition(request.profileId, request.fileId, position, knownDuration, Library.now())
        }
        val finished = position != null && isFinished(position, knownDuration)

        val result = JSONObject()
            .put("title", request.titleId)
            .put("file", request.fileId)
            .put("finished", finished)
            .put("next", JSONObject.NULL)
        val profile = state.profile(request.profileId) ?: return result
        val (title, index) = catalog.locate(request.fileId) ?: return result
        if (finished && title.kind == TitleKind.SHOW && index + 1 < title.items.size) {
            val item = title.items[index + 1]
            val next = catalog.findFile(item.fileIds.first())
            result.put(
                "next",
                JSONObject()
                    .put("title", title.id)
                    .put("name", title.name)
                    .put("file_id", item.fileIds.first())
                    .put("label", episodeLabel(item))
                    .put("episode_title", next?.metadata?.episodeTitle ?: JSONObject.NULL)
                    .put("image", View(catalog, profile).image(item.fileIds.first())),
            )
        }
        return result
    }

    private fun watched(body: JSONObject): JSONObject {
        val profile = requireProfile(body.optString("profile"))
        val title = library.catalog.findTitle(body.optString("title")) ?: throw HttpError(404, "no such title")
        // Every copy, so the mark sticks whichever one plays next.
        val fileId = body.optString("file")
        val files = title.items.filter { fileId.isEmpty() || fileId in it.fileIds }.flatMap { it.fileIds }
        state.setWatched(profile.id, files, body.optBoolean("watched", true), Library.now())
        return ok()
    }

    // ---- State & settings ----

    private fun stateJson(): JSONObject {
        val profiles = JSONArray()
        state.profiles().forEach { profiles.put(profileJson(it)) }
        return JSONObject()
            .put("profiles", profiles)
            .put("settings", settingsJson(settings.get()))
            .put("platform", "android")
            .put("version", version)
    }

    private fun statusJson(): JSONObject {
        val s = library.status
        return JSONObject()
            .put(
                "scan",
                JSONObject()
                    .put("running", s.running)
                    .put("phase", s.phase)
                    .put("files", s.files)
                    .put("done", s.done)
                    .put("total", s.total)
                    .put("version", s.version)
                    .put("finished_at", s.finishedAt),
            )
            .put("titles", library.catalog.titles.size)
            .put("now_playing", JSONObject.NULL)
            .put("permission", host.hasPermission())
    }

    private fun saveSettings(body: JSONObject): JSONObject {
        val before = settings.get()
        val after = Settings(
            minSizeMb = body.optDouble("min_size_mb", before.minSizeMb).coerceAtLeast(0.0),
            includeCamera = body.optBoolean("include_camera", before.includeCamera),
        )
        settings.set(after)
        val rescan = after != before
        if (rescan) library.rescan(after, host.hasPermission())
        return JSONObject().put("settings", settingsJson(after)).put("rescanning", rescan)
    }

    private fun settingsJson(s: Settings) =
        JSONObject().put("min_size_mb", s.minSizeMb).put("include_camera", s.includeCamera)

    private fun profileJson(p: Profile) =
        JSONObject().put("id", p.id).put("name", p.name).put("avatar", p.avatar)

    private fun requireProfile(id: String?): Profile =
        id?.let { state.profile(it) } ?: throw HttpError(404, "no such profile")

    private fun view(profileId: String?) = View(library.catalog, requireProfile(profileId))

    private fun ok() = JSONObject().put("ok", true)

    // ---- Describing titles for one profile ----

    private inner class View(val catalog: Catalog, val profile: Profile) {
        fun image(fileId: String): Any =
            library.thumbnailVersion(fileId)?.let { "/thumbs/$fileId.jpg?v=$it" } ?: JSONObject.NULL

        /** Title art: the first few episodes' (or the movie's) frames. */
        fun titleImage(t: Title): Any {
            for (item in t.items.take(6)) {
                for (id in item.fileIds) {
                    val img = image(id)
                    if (img != JSONObject.NULL) return img
                }
            }
            return JSONObject.NULL
        }

        private fun best(t: Title) = catalog.findFile(t.items.first().fileIds.first())

        fun card(t: Title, up: UpNext = computeUpNext(catalog, profile, t)): JSONObject {
            val best = best(t)
            val card = JSONObject()
                .put("id", t.id)
                .put("name", t.name)
                .put("kind", if (t.kind == TitleKind.SHOW) "show" else "movie")
                .put("year", t.year ?: JSONObject.NULL)
                .put("image", titleImage(t))
                .put("quality", quality(best))
                .put("hdr", false)
                .put("in_list", t.id in profile.myList)
                .put("added", t.added)
            if (t.kind == TitleKind.SHOW) {
                card.put("seasons", t.items.mapNotNull { it.season }.toSet().size)
                card.put("episodes", t.items.size)
            } else {
                card.put("runtime", best?.duration ?: 0.0)
            }
            val upFile = catalog.findFile(up.fileId)
            card.put(
                "up_next",
                JSONObject()
                    .put("file_id", up.fileId)
                    .put("label", episodeLabel(t.items[up.item]))
                    .put("episode_title", upFile?.metadata?.episodeTitle ?: JSONObject.NULL)
                    .put("position", up.position)
                    .put("duration", up.duration)
                    .put("reason", up.reason.json)
                    .put("image", image(up.fileId)),
            )
            return card
        }

        fun item(t: Title, index: Int): JSONObject {
            val item = t.items[index]
            val best = catalog.findFile(item.fileIds.first())
            var latest: Progress? = null
            var img: Any = JSONObject.NULL
            val versions = JSONArray()
            for (id in item.fileIds) {
                profile.progress[id]?.let { if (latest == null || it.updated > latest!!.updated) latest = it }
                if (img == JSONObject.NULL) img = image(id)
                val f = catalog.findFile(id) ?: continue
                versions.put(
                    JSONObject()
                        .put("file_id", f.id)
                        .put("path", f.path)
                        .put("size", f.size)
                        .put("quality", quality(f))
                        .put("width", f.width)
                        .put("height", f.height)
                        .put("codec", JSONObject.NULL)
                        .put("hdr", false)
                        .put("source", f.metadata.source ?: JSONObject.NULL)
                        .put("tags", JSONArray(f.metadata.tags)),
                )
            }
            val progress = latest?.let {
                JSONObject().put("position", it.position).put("duration", it.duration).put("completed", it.completed)
            } ?: JSONObject.NULL
            return JSONObject()
                .put("file_id", item.fileIds.first())
                .put("season", item.season ?: JSONObject.NULL)
                .put("episode", item.episode ?: JSONObject.NULL)
                .put("label", episodeLabel(item))
                .put("episode_title", best?.metadata?.episodeTitle ?: JSONObject.NULL)
                .put("duration", best?.duration ?: 0.0)
                .put("image", img)
                .put("quality", quality(best))
                .put("progress", progress)
                .put("versions", versions)
        }

        fun detail(id: String): JSONObject {
            val t = catalog.findTitle(id) ?: throw HttpError(404, "no such title")
            val items = JSONArray()
            t.items.indices.forEach { items.put(item(t, it)) }
            val folder = best(t)?.path?.substringBeforeLast('/', "") ?: ""
            return card(t).put("items", items).put("folder", folder)
        }

        fun myList(): JSONObject {
            val cards = JSONArray()
            profile.myList.entries.sortedByDescending { it.value }
                .mapNotNull { catalog.findTitle(it.key) }
                .forEach { cards.put(card(it)) }
            return JSONObject().put("items", cards)
        }

        fun search(raw: String): JSONObject {
            val q = simplify(raw)
            val cards = JSONArray()
            if (q.isNotEmpty()) {
                catalog.titles.mapNotNull { t ->
                    val name = simplify(t.name)
                    val rank = when {
                        name.startsWith(q) -> 0
                        name.contains(" $q") -> 1
                        name.contains(q) -> 2
                        t.items.any { item ->
                            catalog.findFile(item.fileIds.first())?.metadata?.episodeTitle
                                ?.let { simplify(it).contains(q) } == true
                        } -> 3
                        else -> return@mapNotNull null
                    }
                    rank to t
                }.sortedBy { it.first }.forEach { cards.put(card(it.second)) }
            }
            return JSONObject().put("items", cards)
        }

        fun home(filter: String, everyone: List<Profile>): JSONObject {
            fun keep(t: Title) = when (filter) {
                "show" -> t.kind == TitleKind.SHOW
                "movie" -> t.kind == TitleKind.MOVIE
                else -> true
            }
            val titles = catalog.titles.filter(::keep)
            val up = titles.associate { it.id to computeUpNext(catalog, profile, it) }
            val rows = JSONArray()

            fun addRow(id: String, name: String, list: List<Title>, minItems: Int, type: String = "titles") {
                if (list.isEmpty() || list.size < minItems) return
                val cards = JSONArray()
                list.take(30).forEach { cards.put(card(it, up.getValue(it.id))) }
                rows.put(JSONObject().put("id", id).put("name", name).put("type", type).put("items", cards))
            }

            addRow(
                "continue", "Continue Watching for ${profile.name}",
                continueWatching(catalog, profile).map { it.title }.filter(::keep), 1, "continue",
            )
            addRow(
                "my-list", "My List",
                profile.myList.entries.sortedByDescending { it.value }
                    .mapNotNull { catalog.findTitle(it.key) }.filter(::keep),
                1,
            )

            // Most played across every profile on the phone.
            val plays = mutableMapOf<String, Int>()
            for (p in everyone) {
                for ((fid, pr) in p.progress) {
                    catalog.locate(fid)?.let { plays.merge(it.first.id, pr.plays, Int::plus) }
                }
            }
            addRow(
                "top10", "Top 10 on Your Phone",
                titles.filter { (plays[it.id] ?: 0) > 0 }.sortedByDescending { plays[it.id] }.take(10), 3, "top10",
            )

            val recent = titles.sortedByDescending { it.added }.take(20)
            addRow("recent", "Recently Added", recent, 1)

            // Untouched titles in a different order each day.
            val day = Library.now() / 86400
            addRow(
                "fresh", "New to You",
                titles.filter { up.getValue(it.id).lastActivity == 0L }.shuffled(Random(day)), 4,
            )

            if (filter == "all") {
                addRow("shows", "TV Shows", titles.filter { it.kind == TitleKind.SHOW }, 1)
                addRow("movies", "Movies", titles.filter { it.kind == TitleKind.MOVIE }, 1)
            } else {
                addRow("all", if (filter == "show") "All TV Shows" else "All Movies", titles, 1)
            }

            addRow("binge", "Bingeworthy Series", titles.filter { it.kind == TitleKind.SHOW && it.items.size >= 8 }, 2)
            addRow("uhd", "Stunning in 4K", titles.filter { (best(it)?.resolution ?: 0) >= 1600 }, 2)
            addRow(
                "epic", "Epic Movies",
                titles.filter { it.kind == TitleKind.MOVIE && (best(it)?.duration ?: 0.0) >= 2.5 * 3600 }, 2,
            )
            addRow(
                "quick", "Quick Watches",
                titles.filter { t -> t.kind == TitleKind.MOVIE && (best(t)?.duration ?: 0.0).let { it > 0 && it < 6000 } },
                2,
            )
            titles.filter { it.year != null }.groupBy { it.year!! / 10 * 10 }.toSortedMap(reverseOrder())
                .forEach { (decade, list) -> addRow("decade-$decade", "From the ${decade}s", list, 3) }
            addRow(
                "again", "Watch It Again",
                titles.filter { up.getValue(it.id).let { u -> u.reason == UpNext.Reason.REWATCH && u.lastActivity > 0 } }
                    .sortedByDescending { up.getValue(it.id).lastActivity },
                1,
            )

            // Billboard: something unwatched with artwork, rotating daily.
            var billboard = recent.filter { titleImage(it) != JSONObject.NULL && up.getValue(it.id).lastActivity == 0L }
            if (billboard.isEmpty()) billboard = recent.filter { titleImage(it) != JSONObject.NULL }
            if (billboard.isEmpty()) billboard = recent.take(1)
            billboard = billboard.take(8)
            val hero = billboard.takeIf { it.isNotEmpty() }
                ?.let { card(it[(day % it.size).toInt()], up.getValue(it[(day % it.size).toInt()].id)) }

            return JSONObject().put("hero", hero ?: JSONObject.NULL).put("rows", rows).put("count", titles.size)
        }
    }

    companion object {
        fun episodeLabel(item: TitleItem): String =
            if (item.season != null && item.episode != null) "S${item.season}:E${item.episode}" else ""

        fun quality(f: VideoFile?): Any = when {
            f == null || f.resolution <= 0 -> JSONObject.NULL
            f.resolution >= 1600 -> "4K"
            f.resolution >= 700 -> "HD"
            else -> "SD"
        }

        fun playerLabel(catalog: Catalog, t: Title, index: Int): String {
            if (t.kind == TitleKind.MOVIE) return t.year?.let { "${t.name} ($it)" } ?: t.name
            val item = t.items[index]
            val episodeTitle = catalog.findFile(item.fileIds.first())?.metadata?.episodeTitle
            return listOfNotNull(t.name, episodeLabel(item), episodeTitle).joinToString(" · ")
        }

        /** Lowercase words separated by single spaces, for forgiving search. */
        fun simplify(s: String): String =
            s.lowercase().replace(Regex("[^a-z0-9]+"), " ").trim()
    }
}
