// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

/** A video on the device plus everything known about it. */
data class VideoFile(
    val id: String,          // fileId(path): stable across scans
    val path: String,        // filesystem path, for naming and the folder shown
    val uri: String,         // content:// URI the player opens
    val size: Long,          // bytes
    val mtime: Long,         // unix seconds; doubles as "date added"
    val metadata: ParsedFilename,
    val duration: Double,    // seconds; 0 if unknown
    val width: Int,
    val height: Int,
) {
    /** The short side, so portrait-recorded video still rates as HD/4K. */
    val resolution: Int get() = minOf(width, height)
}

/** 16 hex digits of FNV-1a over the path, the same scheme as the desktop app. */
fun fileId(path: String): String {
    var hash = 0xcbf29ce484222325uL
    for (byte in path.toByteArray(Charsets.UTF_8)) {
        hash = hash xor (byte.toUByte().toULong())
        hash *= 0x100000001b3uL
    }
    return hash.toString(16).padStart(16, '0')
}

enum class TitleKind { MOVIE, SHOW }

/** A movie, or one episode; several copies are listed best first. */
data class TitleItem(val season: Int?, val episode: Int?, val fileIds: List<String>)

data class Title(
    val id: String,
    val kind: TitleKind,
    val name: String,
    val year: Int?,
    val items: List<TitleItem>,  // movies: one; shows: in watch order
    val added: Long,             // newest file's mtime
)

/**
 * The library grouped into titles: episodes by series name, movies by name
 * and year. A port of the desktop Catalog (src/catalog.cpp).
 */
class Catalog(val files: List<VideoFile>) {
    val titles: List<Title>
    private val fileIndex = files.associateBy { it.id }
    private val titleIndex: Map<String, Title>
    private val fileLocation = mutableMapOf<String, Pair<Title, Int>>()

    init {
        class Group(val kind: TitleKind) {
            var year: Int? = null
            val files = mutableListOf<VideoFile>()
            val names = mutableMapOf<String, Int>()  // spelling -> occurrences
        }
        val groups = sortedMapOf<String, Group>()

        for (f in files) {
            val m = f.metadata
            val isEpisode = m.season != null && m.episode != null
            var name = m.title ?: fallbackName(f)
            var key = normalizeKey(name)
            if (key.isEmpty()) {
                name = FilenameParser.stem(f.path.substringAfterLast('/'))
                key = normalizeKey(name)
            }
            key = (if (isEpisode) "show:" else "movie:") + key
            if (!isEpisode && m.year != null) key += "|${m.year}"

            val g = groups.getOrPut(key) { Group(if (isEpisode) TitleKind.SHOW else TitleKind.MOVIE) }
            g.files += f
            g.names[name] = (g.names[name] ?: 0) + 1
            if (m.year != null && (g.year == null || m.year < g.year!!)) g.year = m.year
        }

        val built = groups.map { (key, g) ->
            // Most common spelling, preferring ones with capitals.
            val best = g.names.entries.maxWith(
                compareBy<Map.Entry<String, Int>> { e -> e.key.any { it.isUpperCase() } }.thenBy { it.value },
            )
            val name = if (best.key.any { it.isUpperCase() }) best.key else titleCase(best.key)

            val items = if (g.kind == TitleKind.MOVIE) {
                listOf(TitleItem(null, null, g.files.sortedWith(betterCopy).map { it.id }))
            } else {
                // Specials (season 0) go after the regular seasons.
                g.files.groupBy { it.metadata.season!! to it.metadata.episode!! }
                    .toSortedMap(compareBy<Pair<Int, Int>> { if (it.first == 0) Int.MAX_VALUE else it.first }
                        .thenBy { it.second })
                    .map { (se, copies) -> TitleItem(se.first, se.second, copies.sortedWith(betterCopy).map { it.id }) }
            }
            Title(
                id = fileId(key),
                kind = g.kind,
                name = name,
                year = g.year,
                items = items,
                added = g.files.maxOf { it.mtime },
            )
        }

        titles = built.sortedBy { normalizeKey(it.name) }
        titleIndex = titles.associateBy { it.id }
        for (t in titles) {
            t.items.forEachIndexed { i, item -> item.fileIds.forEach { fileLocation[it] = t to i } }
        }
    }

    fun findFile(id: String): VideoFile? = fileIndex[id]
    fun findTitle(id: String): Title? = titleIndex[id]
    /** The title holding a file and the index of its item. */
    fun locate(fileId: String): Pair<Title, Int>? = fileLocation[fileId]

    companion object {
        private val seasonDir = Regex("""^(season|series|s)[\s._-]*\d+$""", RegexOption.IGNORE_CASE)

        /** Highest resolution first, then the largest (least compressed). */
        private val betterCopy = compareByDescending<VideoFile> { it.resolution }
            .thenByDescending { it.size }
            .thenBy { it.path }

        /** "Rick & Morty" and "rick.and.morty" share a key. */
        fun normalizeKey(name: String): String {
            val out = StringBuilder()
            var space = false
            for (c in name) {
                when {
                    c == '&' -> {
                        out.append(if (out.isEmpty() || space) "and " else " and ")
                        space = true
                    }
                    c == '\'' -> Unit
                    c.isLetterOrDigit() && c.code < 128 -> {
                        out.append(c.lowercaseChar())
                        space = false
                    }
                    out.isNotEmpty() && !space -> {
                        out.append(' ')
                        space = true
                    }
                }
            }
            return out.toString().trimEnd()
        }

        fun titleCase(s: String): String {
            var start = true
            return buildString {
                for (c in s) {
                    if (c.isLetter()) {
                        append(if (start) c.uppercaseChar() else c)
                        start = false
                    } else {
                        append(c)
                        start = c == ' ' || c == '-'
                    }
                }
            }
        }

        /** For files whose names carry no title: "Show/Season 1/S01E01.mkv" -> "Show". */
        fun fallbackName(file: VideoFile): String {
            val parts = file.path.split('/').filter { it.isNotEmpty() }
            if (file.metadata.season != null && parts.size >= 2) {
                var dir = parts.size - 2
                if (seasonDir.matches(parts[dir]) && dir > 0) dir--
                return parts[dir]
            }
            return FilenameParser.stem(parts.lastOrNull() ?: file.path)
        }
    }
}
