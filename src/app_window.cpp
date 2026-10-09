// Synaxis: a simple offline media aggregator and player
// Copyright (C) 2026  Azhar Tanweer (azhar.tanweer404@gmail.com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "app_window.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <algorithm>
#include <cstdlib>

namespace synaxis {

namespace {

// Matches the installed .desktop file's name, which is how the desktop
// (on Wayland especially) pairs the window with its icon and launcher.
constexpr const char* kAppId = "io.synaxis.Synaxis";
constexpr int kDefaultWidth = 1360;
constexpr int kDefaultHeight = 860;

bool DebugMode() {
    const char* debug = std::getenv("SYNAXIS_DEBUG");
    return debug && *debug && std::string(debug) != "0";
}

// Keep the window on our own UI; anything else opens in the browser.
gboolean OnDecidePolicy(WebKitWebView*, WebKitPolicyDecision* decision,
                        WebKitPolicyDecisionType type, gpointer data) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION &&
        type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) {
        return FALSE;
    }
    auto* action = webkit_navigation_policy_decision_get_navigation_action(
        WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    const std::string& base = *static_cast<const std::string*>(data);
    bool ours = uri && std::string(uri).rfind(base, 0) == 0;
    if (ours && type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;

    if (uri && !ours) g_app_info_launch_default_for_uri(uri, nullptr, nullptr);
    webkit_policy_decision_ignore(decision);
    return TRUE;
}

// No "Reload / Inspect Element" menu: it should feel like an app, not a page.
gboolean OnContextMenu(WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*,
                       gpointer) {
    return !DebugMode();
}

} // namespace

AppWindow::AppWindow(std::filesystem::path state_file) : state_file_(std::move(state_file)) {
    // WebKitGTK's DMA-BUF renderer can leave the window blank or flickering
    // on NVIDIA's proprietary driver. Its fallback is plenty fast for this UI.
    std::error_code ec;
    if (std::filesystem::exists("/sys/module/nvidia", ec)) {
        setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", /*overwrite=*/0);
    }

    g_set_prgname("synaxis");
    g_set_application_name("Synaxis");
    app_ = gtk_application_new(kAppId, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app_, "activate", G_CALLBACK(OnActivate), this);

    // Without a session bus there's no single-instance check; just run.
    GError* error = nullptr;
    if (!g_application_register(G_APPLICATION(app_), nullptr, &error)) g_clear_error(&error);
}

AppWindow::~AppWindow() {
    g_object_unref(app_);
}

bool AppWindow::remote() const {
    return g_application_get_is_registered(G_APPLICATION(app_)) &&
           g_application_get_is_remote(G_APPLICATION(app_));
}

int AppWindow::Run(const std::string& url) {
    url_ = url;
    char name[] = "synaxis";
    char* argv[] = {name, nullptr};
    return g_application_run(G_APPLICATION(app_), 1, argv);
}

void AppWindow::Quit() {
    g_idle_add(
        [](gpointer app) -> gboolean {
            g_application_quit(G_APPLICATION(app));
            return G_SOURCE_REMOVE;
        },
        app_);
}

void AppWindow::OnActivate(GtkApplication*, void* data) {
    auto* self = static_cast<AppWindow*>(data);
    if (self->window_) {
        gtk_window_present(GTK_WINDOW(self->window_));
    } else {
        self->BuildWindow();
    }
}

void AppWindow::BuildWindow() {
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, nullptr);

    window_ = gtk_application_window_new(app_);
    GtkWindow* window = GTK_WINDOW(window_);
    gtk_window_set_title(window, "Synaxis");
    gtk_window_set_icon_name(window, "synaxis");

    int width = kDefaultWidth, height = kDefaultHeight;
    bool maximized = false;
    GKeyFile* keys = g_key_file_new();
    if (g_key_file_load_from_file(keys, state_file_.c_str(), G_KEY_FILE_NONE, nullptr)) {
        width = std::max(640, g_key_file_get_integer(keys, "window", "width", nullptr));
        height = std::max(420, g_key_file_get_integer(keys, "window", "height", nullptr));
        maximized = g_key_file_get_boolean(keys, "window", "maximized", nullptr);
    }
    g_key_file_free(keys);
    gtk_window_set_default_size(window, width, height);
    if (maximized) gtk_window_maximize(window);

    view_ = webkit_web_view_new();
    WebKitWebView* view = WEBKIT_WEB_VIEW(view_);
    // Paint dark before the page loads, so the window doesn't flash white.
    GdkRGBA background = {12 / 255.0, 12 / 255.0, 15 / 255.0, 1.0};
    webkit_web_view_set_background_color(view, &background);
    WebKitSettings* settings = webkit_web_view_get_settings(view);
    webkit_settings_set_enable_developer_extras(settings, DebugMode());
    webkit_settings_set_javascript_can_access_clipboard(settings, FALSE);

    g_signal_connect(view, "decide-policy", G_CALLBACK(OnDecidePolicy), &url_);
    g_signal_connect(view, "context-menu", G_CALLBACK(OnContextMenu), nullptr);

    // F11 fullscreen, Ctrl+Q quit, Ctrl+R / F5 reload.
    g_signal_connect(window_, "key-press-event",
                     G_CALLBACK(+[](GtkWidget* widget, GdkEventKey* event, gpointer data) -> gboolean {
                         auto* self = static_cast<AppWindow*>(data);
                         bool ctrl = event->state & GDK_CONTROL_MASK;
                         if (event->keyval == GDK_KEY_F11) {
                             if (self->fullscreen_) gtk_window_unfullscreen(GTK_WINDOW(widget));
                             else gtk_window_fullscreen(GTK_WINDOW(widget));
                             return TRUE;
                         }
                         if (ctrl && (event->keyval == GDK_KEY_q || event->keyval == GDK_KEY_Q)) {
                             gtk_window_close(GTK_WINDOW(widget));
                             return TRUE;
                         }
                         if (event->keyval == GDK_KEY_F5 ||
                             (ctrl && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R))) {
                             webkit_web_view_reload(WEBKIT_WEB_VIEW(self->view_));
                             return TRUE;
                         }
                         return FALSE;
                     }),
                     this);
    g_signal_connect(window_, "window-state-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventWindowState* event, gpointer data) -> gboolean {
                         auto* self = static_cast<AppWindow*>(data);
                         self->fullscreen_ = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
                         self->maximized_ = event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED;
                         return FALSE;
                     }),
                     this);
    g_signal_connect(window_, "delete-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEvent*, gpointer data) -> gboolean {
                         static_cast<AppWindow*>(data)->SaveGeometry();
                         return FALSE;
                     }),
                     this);

    gtk_container_add(GTK_CONTAINER(window_), view_);
    webkit_web_view_load_uri(view, url_.c_str());
    gtk_widget_show_all(window_);
}

void AppWindow::SaveGeometry() {
    GKeyFile* keys = g_key_file_new();
    g_key_file_load_from_file(keys, state_file_.c_str(), G_KEY_FILE_NONE, nullptr);
    // A maximized/fullscreen size isn't useful to restore to; keep the last normal one.
    if (!maximized_ && !fullscreen_) {
        int width = 0, height = 0;
        gtk_window_get_size(GTK_WINDOW(window_), &width, &height);
        g_key_file_set_integer(keys, "window", "width", width);
        g_key_file_set_integer(keys, "window", "height", height);
    }
    g_key_file_set_boolean(keys, "window", "maximized", maximized_);
    g_key_file_save_to_file(keys, state_file_.c_str(), nullptr);
    g_key_file_free(keys);
}

} // namespace synaxis
