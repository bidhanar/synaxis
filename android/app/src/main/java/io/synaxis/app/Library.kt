// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

import android.content.ContentUris
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Color
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.provider.MediaStore
import org.json.JSONObject
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.sqrt

data class Settings(
    val minSizeMb: Double = 200.0,  // skip trailers, samples and clips
    val includeCamera: Boolean = false,  // phone recordings aren't movies
)

/** settings.json in the app's private storage. */
class SettingsStore(private val file: File) {
    @Volatile private var current: Settings = load()

    fun get(): Settings = current

    fun set(settings: Settings) {
        current = settings
        file.writeText(
            JSONObject()
                .put("min_size_mb", settings.minSizeMb)
                .put("include_camera", settings.includeCamera)
                .toString(1),
        )
    }

    private fun load(): Settings = try {
        val j = JSONObject(file.readText())
        Settings(j.optDouble("min_size_mb", 200.0), j.optBoolean("include_camera", false))
    } catch (e: Exception) {
        Settings()
    }
}

/**
 * Keeps the catalog current in the background: asks MediaStore for the
 * device's videos and renders thumbnails. Readers get immutable snapshots.
 */
class Library(private val context: Context, private val thumbsDir: File) {
    data class Status(
        val running: Boolean = false,
        val phase: String = "idle",  // "scanning", "thumbnails" or "idle"
        val files: Int = 0,
        val done: Int = 0,
        val total: Int = 0,
        val version: Long = 1,       // bumps when the catalog changes
        val finishedAt: Long = 0,
    )

    @Volatile var catalog: Catalog = Catalog(emptyList())
        private set
    @Volatile var status = Status()
        private set

    private val worker = Executors.newSingleThreadExecutor { r ->
        Thread(r, "synaxis-library").apply { priority = Thread.MIN_PRIORITY }
    }
    private val queued = AtomicBoolean(false)
    private val failedThumbs = ConcurrentHashMap.newKeySet<String>()
    private var signature: List<Any> = emptyList()

    init {
        thumbsDir.mkdirs()
    }

    /** Queues a scan; repeated calls while one is waiting collapse into one. */
    fun rescan(settings: Settings, hasPermission: Boolean) {
        if (!queued.compareAndSet(false, true)) return
        status = status.copy(running = true)
        worker.execute {
            queued.set(false)
            try {
                scan(settings, hasPermission)
            } finally {
                status = status.copy(running = false, phase = "idle", done = 0, total = 0, finishedAt = now())
            }
        }
    }

    fun retryThumbnails() = failedThumbs.clear()

    fun thumbnailFile(fileId: String) = File(thumbsDir, "$fileId.jpg")

    /** Modification time of a file's thumbnail (for cache-busting URLs), if one exists. */
    fun thumbnailVersion(fileId: String): Long? =
        thumbnailFile(fileId).takeIf { it.exists() }?.lastModified()?.div(1000)

    private fun scan(settings: Settings, hasPermission: Boolean) {
        status = status.copy(phase = "scanning")
        val files = if (hasPermission) queryVideos(settings) else emptyList()

        // Only a real change bumps the version, so the UI doesn't redraw on
        // every return to the app.
        val newSignature = files.map { listOf(it.id, it.size, it.mtime) }
        catalog = Catalog(files)
        if (newSignature != signature) {
            signature = newSignature
            status = status.copy(files = files.size, version = status.version + 1)
        }
        renderThumbnails(files)
    }

    private fun queryVideos(settings: Settings): List<VideoFile> {
        val collection = MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL)
        val projection = arrayOf(
            MediaStore.Video.Media._ID,
            MediaStore.Video.Media.DATA,
            MediaStore.Video.Media.DISPLAY_NAME,
            MediaStore.Video.Media.SIZE,
            MediaStore.Video.Media.DURATION,
            MediaStore.Video.Media.WIDTH,
            MediaStore.Video.Media.HEIGHT,
            MediaStore.Video.Media.DATE_MODIFIED,
            MediaStore.Video.Media.RELATIVE_PATH,
        )
        val minBytes = (settings.minSizeMb.coerceAtLeast(0.0) * 1024 * 1024).toLong()
        val files = mutableListOf<VideoFile>()
        val cursor = try {
            context.contentResolver.query(
                collection, projection, "${MediaStore.Video.Media.SIZE} >= ?",
                arrayOf(minBytes.toString()), null,
            )
        } catch (e: SecurityException) {
            null
        } ?: return files

        cursor.use { c ->
            val idCol = c.getColumnIndexOrThrow(MediaStore.Video.Media._ID)
            val dataCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.DATA)
            val nameCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.DISPLAY_NAME)
            val sizeCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.SIZE)
            val durationCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.DURATION)
            val widthCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.WIDTH)
            val heightCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.HEIGHT)
            val modifiedCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.DATE_MODIFIED)
            val relativeCol = c.getColumnIndexOrThrow(MediaStore.Video.Media.RELATIVE_PATH)
            while (c.moveToNext()) {
                val name = c.getString(nameCol) ?: continue
                val relative = c.getString(relativeCol) ?: ""
                if (!settings.includeCamera && isRecording(relative)) continue
                val path = c.getString(dataCol)?.takeIf { it.isNotEmpty() }
                    ?: "/storage/emulated/0/$relative$name"
                files += VideoFile(
                    id = fileId(path),
                    path = path,
                    uri = ContentUris.withAppendedId(collection, c.getLong(idCol)).toString(),
                    size = c.getLong(sizeCol),
                    mtime = c.getLong(modifiedCol),
                    metadata = FilenameParser.parse(name),
                    duration = c.getLong(durationCol) / 1000.0,
                    width = c.getInt(widthCol),
                    height = c.getInt(heightCol),
                )
            }
        }
        return files.sortedBy { it.path }
    }

    private fun isRecording(relativePath: String): Boolean =
        relativePath.startsWith("DCIM/", ignoreCase = true) ||
            relativePath.contains("screen record", ignoreCase = true) ||
            relativePath.contains("screenrecord", ignoreCase = true)

    private fun renderThumbnails(files: List<VideoFile>) {
        val todo = files
            .filter { it.id !in failedThumbs }
            .filter { (thumbnailVersion(it.id) ?: -1) < it.mtime }
            .sortedByDescending { it.mtime }  // newest first: the home screen shows those first
        status = status.copy(phase = "thumbnails", done = 0, total = todo.size)
        for (f in todo) {
            if (queued.get()) return  // a newer scan is waiting
            if (!writeThumbnail(f, thumbnailFile(f.id))) failedThumbs += f.id
            status = status.copy(done = status.done + 1)
        }
        // Thumbnails of videos that are gone.
        val live = files.mapTo(HashSet()) { "${it.id}.jpg" }
        thumbsDir.listFiles()?.filter { it.name !in live }?.forEach { it.delete() }
    }

    /**
     * Picks a representative frame, skipping dark or flat ones (intros,
     * fades, title cards) and cropping letterbox bars. Port of WriteThumbnail
     * in src/media_probe.cpp.
     */
    private fun writeThumbnail(file: VideoFile, out: File): Boolean {
        val retriever = MediaMetadataRetriever()
        try {
            retriever.setDataSource(context, Uri.parse(file.uri))
            val durationUs = (file.duration * 1_000_000).toLong()
            var best: Bitmap? = null
            var bestScore = -1.0
            for (point in doubleArrayOf(0.15, 0.25, 0.40, 0.55, 0.08)) {
                val frame = if (durationUs > 0) {
                    retriever.getScaledFrameAtTime(
                        (durationUs * point).toLong(), MediaMetadataRetriever.OPTION_CLOSEST_SYNC, 960, 960,
                    )
                } else {
                    retriever.frameAtTime
                } ?: continue
                val cropped = cropBars(frame)
                if (cropped !== frame) frame.recycle()
                val (mean, stddev) = lumaStats(cropped)
                val score = min(mean, 110.0) + stddev
                if (score > bestScore) {
                    best?.recycle()
                    best = cropped
                    bestScore = score
                } else {
                    cropped.recycle()
                }
                if ((mean >= 45 && stddev >= 25) || durationUs <= 0) break
            }
            val image = best ?: return false
            val tmp = File(out.path + ".tmp")
            tmp.outputStream().use { image.compress(Bitmap.CompressFormat.JPEG, 85, it) }
            image.recycle()
            return tmp.renameTo(out)
        } catch (e: Exception) {
            return false
        } finally {
            retriever.release()
        }
    }

    companion object {
        fun now(): Long = System.currentTimeMillis() / 1000

        private fun luma(c: Int): Double =
            0.299 * Color.red(c) + 0.587 * Color.green(c) + 0.114 * Color.blue(c)

        private fun lumaStats(b: Bitmap): Pair<Double, Double> {
            val w = b.width
            val h = b.height
            val px = IntArray(w * h)
            b.getPixels(px, 0, w, 0, 0, w, h)
            var sum = 0.0
            var sumSq = 0.0
            var n = 0
            for (y in 0 until h step 4) {
                for (x in 0 until w step 4) {
                    val l = luma(px[y * w + x])
                    sum += l
                    sumSq += l * l
                    n++
                }
            }
            if (n == 0) return 0.0 to 0.0
            val mean = sum / n
            return mean to sqrt((sumSq / n - mean * mean).coerceAtLeast(0.0))
        }

        /**
         * Trims letterbox/pillarbox bars. They only count when both opposite
         * edges have them at about the same size, so a dark scene with a black
         * sky isn't mistaken for letterboxing.
         */
        private fun cropBars(b: Bitmap): Bitmap {
            val w = b.width
            val h = b.height
            val px = IntArray(w * h)
            b.getPixels(px, 0, w, 0, 0, w, h)

            fun dark(x0: Int, y0: Int, dx: Int, dy: Int, count: Int): Boolean {
                var bright = 0
                var i = 0
                while (i < count) {
                    if (luma(px[(y0 + i * dy) * w + x0 + i * dx]) > 40) bright++
                    i += 2
                }
                return bright * 2 < count / 50 + 1
            }

            var top = 0
            var bottom = h
            var left = 0
            var right = w
            while (top < h / 3 && dark(0, top, 1, 0, w)) top++
            while (bottom > h * 2 / 3 && dark(0, bottom - 1, 1, 0, w)) bottom--
            while (left < w / 3 && dark(left, 0, 0, 1, h)) left++
            while (right > w * 2 / 3 && dark(right - 1, 0, 0, 1, h)) right--

            fun symmetric(a: Int, b: Int, size: Int) =
                a >= size / 50 && b >= size / 50 && abs(a - b) <= size / 50 + 2
            if (!symmetric(top, h - bottom, h)) {
                top = 0
                bottom = h
            }
            if (!symmetric(left, w - right, w)) {
                left = 0
                right = w
            }
            if (top == 0 && left == 0) return b
            return Bitmap.createBitmap(b, left, top, right - left, bottom - top)
        }
    }
}
