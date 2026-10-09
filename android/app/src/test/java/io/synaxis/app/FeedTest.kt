// SPDX-License-Identifier: GPL-3.0-or-later
package io.synaxis.app

import org.junit.Assert.assertEquals
import org.junit.Test

class FeedTest {
    private val e1 = video("/Show.S01E01.mkv")
    private val e2 = video("/Show.S01E02.mkv")
    private val e3 = video("/Show.S01E03.mkv")
    private val film = video("/Film.2010.mkv")
    private val catalog = Catalog(listOf(e1, e2, e3, film))
    private val show = catalog.locate(e1.id)!!.first
    private val movie = catalog.locate(film.id)!!.first

    private fun profile(vararg watched: Pair<VideoFile, Pair<Double, Long>>) = Profile(
        "p", "Test", 0,
        progress = watched.associate { (f, pw) -> f.id to Progress(pw.first, 1000.0, pw.second, isFinished(pw.first, 1000.0), 1) },
    )

    @Test
    fun startResumeNextAndRewatch() {
        assertEquals(UpNext.Reason.START, computeUpNext(catalog, profile(), show).reason)

        val resume = computeUpNext(catalog, profile(e1 to (1000.0 to 10L), e2 to (400.0 to 20L)), show)
        assertEquals(UpNext.Reason.RESUME, resume.reason)
        assertEquals(e2.id, resume.fileId)
        assertEquals(400.0, resume.position, 0.0)

        val barely = computeUpNext(catalog, profile(e2 to (4.0 to 20L)), show)
        assertEquals(UpNext.Reason.START to e2.id, barely.reason to barely.fileId)

        val next = computeUpNext(catalog, profile(e1 to (990.0 to 10L)), show)
        assertEquals(UpNext.Reason.NEXT_EPISODE to e2.id, next.reason to next.fileId)

        val again = computeUpNext(catalog, profile(e3 to (1000.0 to 10L)), show)
        assertEquals(UpNext.Reason.REWATCH to e1.id, again.reason to again.fileId)
    }

    @Test
    fun continueWatchingOrderAndDismissal() {
        val p = profile(e1 to (1000.0 to 10L), film to (300.0 to 50L))
        assertEquals(listOf(movie, show), continueWatching(catalog, p).map { it.title })

        val dismissed = p.copy(dismissed = mapOf(movie.id to 60L))
        assertEquals(listOf(show), continueWatching(catalog, dismissed).map { it.title })
    }
}
