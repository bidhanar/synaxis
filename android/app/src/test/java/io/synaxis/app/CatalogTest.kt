// SPDX-License-Identifier: GPL-3.0-or-later
package io.synaxis.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

fun video(path: String, height: Int = 1080, size: Long = 1_000_000_000, mtime: Long = 100, duration: Double = 1000.0) =
    VideoFile(
        id = fileId(path), path = path, uri = "content://test/${fileId(path)}", size = size, mtime = mtime,
        metadata = FilenameParser.parse(path.substringAfterLast('/')),
        duration = duration, width = height * 16 / 9, height = height,
    )

class CatalogTest {
    @Test
    fun fileIdsMatchTheDesktopApp() {
        // FNV-1a 64 of the path, as in src/catalog.cpp.
        assertEquals("cbf29ce484222325", fileId(""))
        assertEquals("af63dc4c8601ec8c", fileId("a"))
    }

    @Test
    fun groupsEpisodesInWatchOrder() {
        val c = Catalog(listOf(
            video("/m/Show/Season 02/Show.S02E01.mkv"),
            video("/m/Show/Season 01/Show.S01E02.mkv"),
            video("/m/Show/Season 01/show.s01e01.mkv"),
            video("/m/Show.S00E01.mkv"),
        ))
        assertEquals(1, c.titles.size)
        val show = c.titles[0]
        assertEquals("Show", show.name)
        assertEquals(TitleKind.SHOW, show.kind)
        assertEquals(listOf(1 to 1, 1 to 2, 2 to 1, 0 to 1), show.items.map { it.season to it.episode })
    }

    @Test
    fun copiesShareAnItemBestFirstAndYearsSplitMovies() {
        val sd = video("/a/Inception.2010.720p.mkv", height = 720)
        val uhd = video("/b/Inception (2010) [2160p].mkv", height = 2160)
        val c = Catalog(listOf(sd, uhd, video("/Heat.1995.mkv"), video("/Heat.2023.mkv")))
        assertEquals(3, c.titles.size)
        val inception = c.titles.single { it.name == "Inception" }
        assertEquals(listOf(uhd.id, sd.id), inception.items.single().fileIds)
    }

    @Test
    fun namesAndKeys() {
        val c = Catalog(listOf(
            video("/Rick.and.Morty.S01E01.mkv"),
            video("/Rick & Morty - S01E02.mkv"),
            video("/m/Cool Show/Season 1/S01E01.mkv"),
            video("/mad.max.fury.road.2015.mkv"),
        ))
        assertEquals(listOf("Cool Show", "Mad Max Fury Road", "Rick and Morty"), c.titles.map { it.name })
        assertEquals(2, c.titles.last().items.size)
        assertNull(c.locate("nope"))
    }
}
