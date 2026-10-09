// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later

package io.synaxis.app

import android.webkit.JavascriptInterface

/**
 * Exposed to the web UI as `window.SynaxisAndroid`. The UI's API client uses
 * it instead of fetch(), so nothing listens on a network socket. Calls arrive
 * on a WebView binder thread; Api is thread-safe.
 */
class Bridge(private val api: Api) {
    @JavascriptInterface
    fun call(method: String, path: String, body: String): String = api.handle(method, path, body)
}
