// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

/**
 * Metadata recovered from a scene-style filename. A port of the desktop
 * FilenameParser (src/filename_parser.cpp); both are checked against the same
 * fixtures, so keep them in step.
 */
data class ParsedFilename(
    val season: Int? = null,
    val episode: Int? = null,
    val year: Int? = null,
    val source: String? = null,
    val title: String? = null,
    val tags: List<String> = emptyList(),
    val episodeTitle: String? = null,
)

object FilenameParser {
    private val leadingBracket = Regex("""^\s*\[[^\]]*]\s*""")
    private val extraSpace = Regex("""\s+""")
    // "S01E01" / "s01e06".
    private val standard = Regex("""[Ss](\d{1,2})[Ee](\d{1,2})""")
    // "1x04"; \b keeps it out of tokens like "x264".
    private val alternative = Regex("""\b(\d{1,2})x(\d{2})\b""")
    private val year = Regex("""\(?\b(19\d{2}|20\d{2})\b\)?""")
    private val source = Regex(
        """\b(WEB-DL|WEBRip|BluRay|BrRip|BDRip|DVDRip|HDRip|HDTV|HDTS|CAM)\b""",
        RegexOption.IGNORE_CASE,
    )
    private val tag = Regex("""\b(PROPER|REPACK|IMAX|HDR|AMZN|NF)\b""", RegexOption.IGNORE_CASE)
    private val episodeTitleEnd = Regex(
        """\[|\(|\b(?:\d{3,4}[pi]|[xh]\.?26[45]|HEVC|XviD|10.?bit|(?:19|20)\d{2}|""" +
            """WEB-DL|WEBRip|BluRay|BrRip|BDRip|DVDRip|HDRip|HDTV|HDTS|CAM|""" +
            """PROPER|REPACK|IMAX|HDR|AMZN|NF|UHD)\b""",
        RegexOption.IGNORE_CASE,
    )

    /** Strips a leading "[GROUP]" and turns separators into single spaces. */
    fun cleanTitle(raw: String): String {
        var s = leadingBracket.replace(raw, "")
        s = s.replace('.', ' ').replace('_', ' ')
        s = extraSpace.replace(s, " ")
        return s.trim { it == ' ' || it == '-' || it == '.' || it == '_' }
    }

    fun parse(filename: String): ParsedFilename {
        // The leftmost marker of any kind is where the title ends.
        var cutoff: Int? = null
        fun consider(position: Int) {
            if (cutoff == null || position < cutoff!!) cutoff = position
        }

        var season: Int? = null
        var episode: Int? = null
        var markerEnd: Int? = null
        (standard.find(filename) ?: alternative.find(filename))?.let {
            season = it.groupValues[1].toInt()
            episode = it.groupValues[2].toInt()
            consider(it.range.first)
            markerEnd = it.range.last + 1
        }

        var releaseYear: Int? = null
        year.find(filename)?.let {
            releaseYear = it.groupValues[1].toInt()
            consider(it.range.first)
        }

        var ripSource: String? = null
        source.find(filename)?.let {
            ripSource = it.groupValues[1]
            consider(it.range.first)
        }

        val tags = mutableListOf<String>()
        tag.findAll(filename).forEach {
            tags += it.groupValues[1]
            consider(it.range.first)
        }

        val stem = stem(filename)
        val title = cleanTitle(cutoff?.let { filename.substring(0, it) } ?: stem)

        var episodeTitle: String? = null
        markerEnd?.let { end ->
            if (end < stem.length) {
                var rest = stem.substring(end)
                episodeTitleEnd.find(rest)?.let { rest = rest.substring(0, it.range.first) }
                episodeTitle = cleanTitle(rest).ifEmpty { null }
            }
        }

        return ParsedFilename(
            season = season,
            episode = episode,
            year = releaseYear,
            source = ripSource,
            title = title.ifEmpty { null },
            tags = tags,
            episodeTitle = episodeTitle,
        )
    }

    /** The filename without its last extension, like std::filesystem::path::stem. */
    fun stem(filename: String): String {
        val dot = filename.lastIndexOf('.')
        return if (dot > 0) filename.substring(0, dot) else filename
    }
}
