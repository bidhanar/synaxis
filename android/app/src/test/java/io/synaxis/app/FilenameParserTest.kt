// SPDX-License-Identifier: GPL-3.0-or-later
package io.synaxis.app

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.io.File

/** Same ground truth as the desktop parser (tests/fixtures/expected_metadata.json). */
class FilenameParserTest {
    private val fixtures = File(System.getProperty("synaxis.fixtures") ?: "../../tests/fixtures")

    @Test
    fun matchesTheSharedFixtures() {
        val entries = JSONObject(File(fixtures, "expected_metadata.json").readText()).getJSONArray("entries")
        assertEquals(46, entries.length())
        for (i in 0 until entries.length()) {
            val e = entries.getJSONObject(i)
            val name = e.getString("path").substringAfterLast('/')
            val p = FilenameParser.parse(name)
            fun int(key: String) = if (e.isNull(key)) null else e.getInt(key)
            fun str(key: String) = if (e.isNull(key)) null else e.getString(key)
            assertEquals(name, int("season"), p.season)
            assertEquals(name, int("episode"), p.episode)
            assertEquals(name, int("year"), p.year)
            assertEquals(name, str("source"), p.source)
            assertEquals(name, str("title"), p.title)
            val tags = e.getJSONArray("tags").let { a -> (0 until a.length()).map { a.getString(it) } }
            assertEquals(name, tags, p.tags)
        }
    }

    @Test
    fun episodeTitles() {
        assertEquals("Pilot", FilenameParser.parse("Crimson Static - S01E01 - Pilot [720p] [HDTV] [x264].mkv").episodeTitle)
        assertEquals("Erickerhead",
            FilenameParser.parse("Rick.and.Morty.S09E06.Erickerhead.1080p.WEBRip.10Bit.DDP5.1.x265-NeoNoir.mkv").episodeTitle)
        assertEquals("The Long Night", FilenameParser.parse("Show.S02E03.The.Long.Night.720p.mkv").episodeTitle)
        assertNull(FilenameParser.parse("The.Silent.Orbit.S01E01.1080p.WEB-DL.x264-SPARKS.mkv").episodeTitle)
        assertNull(FilenameParser.parse("[TAoE] Crimson Static - 2x09 IMAX [2160p][BluRay][x265].mkv").episodeTitle)
    }

    @Test
    fun commonMovieNames() {
        val yify = FilenameParser.parse("A.Beautiful.Mind.2001.1080p.BrRip.x264.YIFY.mp4")
        assertEquals("A Beautiful Mind", yify.title)
        assertEquals(2001, yify.year)
        assertEquals("BrRip", yify.source)
    }
}
