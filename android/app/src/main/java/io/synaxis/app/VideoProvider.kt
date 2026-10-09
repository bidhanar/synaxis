// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

import android.content.ContentProvider
import android.content.ContentUris
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.MediaStore
import android.provider.OpenableColumns
import java.io.FileNotFoundException

/**
 * Lends one video at a time to the player, read-only, through Synaxis's own
 * media permission.
 *
 * Handing VLC a MediaStore URI directly makes it translate the URI to a file
 * path and open that itself, which fails unless VLC has separately been given
 * storage access. A URI from this provider can only be opened through us, so
 * the player needs nothing beyond the temporary grant on the intent.
 *
 * URIs look like content://io.synaxis.app.videos/<MediaStore video id>.
 */
class VideoProvider : ContentProvider() {
    override fun onCreate() = true

    private fun mediaUri(uri: Uri): Uri {
        val id = uri.lastPathSegment?.toLongOrNull() ?: throw FileNotFoundException(uri.toString())
        return ContentUris.withAppendedId(MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL), id)
    }

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw SecurityException("read-only")
        return context!!.contentResolver.openFileDescriptor(mediaUri(uri), "r")
            ?: throw FileNotFoundException(uri.toString())
    }

    override fun getType(uri: Uri): String =
        runCatching { context!!.contentResolver.getType(mediaUri(uri)) }.getOrNull() ?: "video/*"

    /** Name and size, which players show and use for seeking. */
    override fun query(
        uri: Uri,
        projection: Array<out String>?,
        selection: String?,
        selectionArgs: Array<out String>?,
        sortOrder: String?,
    ): Cursor {
        val columns = arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)
        val result = MatrixCursor(columns)
        context!!.contentResolver.query(mediaUri(uri), columns, null, null, null)?.use { c ->
            if (c.moveToFirst()) result.addRow(arrayOf(c.getString(0), c.getLong(1)))
        }
        return result
    }

    override fun insert(uri: Uri, values: ContentValues?): Uri? = throw UnsupportedOperationException()
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?) = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?) = 0

    companion object {
        const val AUTHORITY = "io.synaxis.app.videos"

        /** The provider URI for a MediaStore video URI. */
        fun forMedia(mediaUri: String): Uri =
            Uri.parse("content://$AUTHORITY/${Uri.parse(mediaUri).lastPathSegment}")
    }
}
