// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

import android.Manifest
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings as AndroidSettings
import android.view.View
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.ActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.io.ByteArrayInputStream
import java.io.File
import java.io.InputStream

class MainActivity : ComponentActivity(), Api.Host {
    private lateinit var web: WebView
    private lateinit var api: Api
    private lateinit var library: Library
    private lateinit var settings: SettingsStore
    private lateinit var state: WatchState
    private val prefs by lazy { getSharedPreferences("synaxis", MODE_PRIVATE) }

    private val player = registerForActivityResult(ActivityResultContracts.StartActivityForResult(), ::onPlayerResult)
    private val permissionRequest =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { rescan() }

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge(SystemBarStyle.dark(Color.TRANSPARENT), SystemBarStyle.dark(Color.TRANSPARENT))
        super.onCreate(savedInstanceState)

        state = WatchState(File(filesDir, "watch_state.json"))
        if (state.profiles().isEmpty()) state.addProfile("Me", 0)
        settings = SettingsStore(File(filesDir, "settings.json"))
        library = Library(this, File(filesDir, "thumbs"))
        val version = packageManager.getPackageInfo(packageName, 0).versionName ?: ""
        api = Api(library, state, settings, this, version)

        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) {
            WebView.setWebContentsDebuggingEnabled(true)
        }
        web = WebView(this).apply {
            setBackgroundColor(BACKGROUND)
            overScrollMode = View.OVER_SCROLL_NEVER
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true  // remembers the chosen profile
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            settings.mediaPlaybackRequiresUserGesture = true
            addJavascriptInterface(Bridge(api), "SynaxisAndroid")
            webViewClient = UiClient()
        }

        // Keep the UI clear of the status bar, navigation bar and camera cutout;
        // the dark container shows behind them.
        val root = FrameLayout(this).apply {
            setBackgroundColor(BACKGROUND)
            addView(web)
        }
        ViewCompat.setOnApplyWindowInsetsListener(root) { view, insets ->
            val bars = insets.getInsets(WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            WindowInsetsCompat.CONSUMED
        }
        setContentView(root)

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                // The UI closes its own dialogs first; then page history; then exit.
                web.evaluateJavascript("window.synaxisBack ? window.synaxisBack() : false") { handled ->
                    if (handled == "true") return@evaluateJavascript
                    if (web.canGoBack()) web.goBack() else finish()
                }
            }
        })

        if (!hasPermission()) requestPermission()
        web.loadUrl("https://$HOST/")
    }

    override fun onResume() {
        super.onResume()
        rescan()  // pick up videos added while we were away
    }

    private fun rescan() = library.rescan(settings.get(), hasPermission())

    // ---- Permission ----

    private fun mediaPermissions(): Array<String> = when {
        Build.VERSION.SDK_INT >= 34 ->
            arrayOf(Manifest.permission.READ_MEDIA_VIDEO, Manifest.permission.READ_MEDIA_VISUAL_USER_SELECTED)
        Build.VERSION.SDK_INT >= 33 -> arrayOf(Manifest.permission.READ_MEDIA_VIDEO)
        else -> arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE)
    }

    override fun hasPermission(): Boolean = mediaPermissions().any {
        ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    override fun requestPermission() {
        runOnUiThread {
            val primary = mediaPermissions().first()
            // Once refused for good, Android won't ask again: send them to settings.
            if (prefs.getBoolean("asked_permission", false) && !shouldShowRequestPermissionRationale(primary)) {
                startActivity(
                    Intent(AndroidSettings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.fromParts("package", packageName, null)),
                )
            } else {
                prefs.edit().putBoolean("asked_permission", true).apply()
                permissionRequest.launch(mediaPermissions())
            }
        }
    }

    // ---- Playback ----

    override fun play(request: PlayRequest) {
        runOnUiThread { launchPlayer(request, otherApp = false) }
    }

    private fun vlcInstalled(): Boolean = try {
        packageManager.getPackageInfo(VLC, 0)
        true
    } catch (e: PackageManager.NameNotFoundException) {
        false
    }

    private fun launchPlayer(request: PlayRequest, otherApp: Boolean) {
        val uri = VideoProvider.forMedia(request.uri)
        val startMs = (request.start * 1000).toLong()
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "video/*")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            putExtra("title", request.label)
        }

        if (!otherApp) {
            if (!vlcInstalled()) return askForPlayer(request)
            // VLC's player activity takes a start position and reports back
            // where playback stopped.
            intent.component = ComponentName(VLC, "org.videolan.vlc.gui.video.VideoPlayerActivity")
            if (startMs > 0) intent.putExtra("position", startMs) else intent.putExtra("from_start", true)
        } else {
            intent.putExtra("position", startMs.toInt())  // understood by several players
        }

        // Remembered on disk: Android may kill us while the player is open.
        prefs.edit().putString("pending_play", request.toJson()).apply()
        state.recordStart(request.profileId, request.fileId, Library.now())
        try {
            player.launch(intent)
        } catch (e: ActivityNotFoundException) {
            prefs.edit().remove("pending_play").apply()
            Toast.makeText(this, R.string.no_player, Toast.LENGTH_LONG).show()
        }
    }

    private fun askForPlayer(request: PlayRequest) {
        AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle(R.string.vlc_needed_title)
            .setMessage(R.string.vlc_needed_message)
            .setPositiveButton(R.string.get_vlc) { _, _ -> openStore() }
            .setNeutralButton(R.string.other_player) { _, _ -> launchPlayer(request, otherApp = true) }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun openStore() {
        for (link in listOf("market://details?id=$VLC", "https://play.google.com/store/apps/details?id=$VLC")) {
            try {
                startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(link)))
                return
            } catch (e: ActivityNotFoundException) {
                // try the next one
            }
        }
    }

    private fun onPlayerResult(result: ActivityResult) {
        val request = prefs.getString("pending_play", null)?.let(PlayRequest::fromJson) ?: return
        prefs.edit().remove("pending_play").apply()

        // VLC reports extra_position/extra_duration; other players often "position".
        val extras = result.data?.extras
        fun millis(vararg keys: String): Long? = keys.firstNotNullOfOrNull {
            @Suppress("DEPRECATION")
            (extras?.get(it) as? Number)?.toLong()
        }
        val position = millis("extra_position", "position")?.takeIf { it >= 0 }?.div(1000.0)
        val duration = millis("extra_duration", "duration")?.takeIf { it > 0 }?.div(1000.0)

        val ended = api.playbackEnded(request, position, duration)
        web.evaluateJavascript("window.synaxisPlaybackEnded && window.synaxisPlaybackEnded($ended)", null)
    }

    // ---- Serving the UI ----

    /** Serves the bundled web UI and thumbnails on a private https origin. */
    private inner class UiClient : WebViewClient() {
        override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
            if (request.url.host != HOST) return null
            val path = request.url.path ?: "/"
            ASSET.matchEntire(path)?.let {
                val name = it.groupValues[1].ifEmpty { "index.html" }
                return respond(MIME.getValue(name.substringAfterLast('.')), assets.open(name), "no-cache")
            }
            THUMB.matchEntire(path)?.let {
                val file = library.thumbnailFile(it.groupValues[1])
                if (file.exists()) return respond("image/jpeg", file.inputStream(), "max-age=31536000, immutable")
            }
            return WebResourceResponse("text/plain", "utf-8", 404, "Not Found", emptyMap(), ByteArrayInputStream(ByteArray(0)))
        }

        // Anything off our origin opens in the browser instead.
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
            if (request.url.host == HOST) return false
            try {
                startActivity(Intent(Intent.ACTION_VIEW, request.url))
            } catch (e: ActivityNotFoundException) {
                // nothing to open it with
            }
            return true
        }

        private fun respond(mime: String, data: InputStream, cache: String) =
            WebResourceResponse(mime, "utf-8", 200, "OK", mapOf("Cache-Control" to cache), data)
    }

    companion object {
        // Reserved for app-local content; never resolves on the network.
        private const val HOST = "appassets.androidplatform.net"
        private const val VLC = "org.videolan.vlc"
        private const val BACKGROUND = 0xFF0C0C0F.toInt()
        private val ASSET = Regex("""/(|index\.html|app\.js|app\.css|icon\.svg)""")
        private val THUMB = Regex("""/thumbs/([0-9a-f]{16})\.jpg""")
        private val MIME = mapOf(
            "html" to "text/html",
            "js" to "text/javascript",
            "css" to "text/css",
            "svg" to "image/svg+xml",
        )
    }
}
