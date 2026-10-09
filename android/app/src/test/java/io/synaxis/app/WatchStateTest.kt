// SPDX-License-Identifier: GPL-3.0-or-later
package io.synaxis.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class WatchStateTest {
    @Test
    fun persistsAcrossReloads() {
        val file = File.createTempFile("synaxis_state", ".json").apply { delete() }
        val state = WatchState(file)
        val id = state.addProfile("Sam", 3).id
        state.recordStart(id, "f1", 100)
        state.recordPosition(id, "f1", 600.0, 3600.0, 101)
        assertTrue(state.toggleMyList(id, "t1", 102))
        state.dismiss(id, "t2", 103)

        val p = WatchState(file).profile(id)!!
        assertEquals("Sam", p.name)
        assertEquals(Progress(600.0, 3600.0, 101, false, 1), p.progress["f1"])
        assertEquals(102L, p.myList["t1"])
        assertEquals(103L, p.dismissed["t2"])
        file.delete()
    }

    @Test
    fun finishingAndMarking() {
        assertTrue(isFinished(3420.0, 3600.0))
        assertFalse(isFinished(3400.0, 3600.0))
        val state = WatchState(File.createTempFile("synaxis_state", ".json").apply { delete(); deleteOnExit() })
        val id = state.addProfile("A", 0).id
        state.recordPosition(id, "f", 3500.0, 3600.0, 1)
        assertTrue(state.profile(id)!!.progress.getValue("f").completed)
        state.setWatched(id, listOf("f"), false, 2)
        assertTrue(state.profile(id)!!.progress.isEmpty())
        assertFalse(state.toggleMyList("ghost", "t", 1))
    }
}
