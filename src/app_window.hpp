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

#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#ifndef _WIN32
typedef struct _GtkApplication GtkApplication;
typedef struct _GtkWidget GtkWidget;
#endif

namespace synaxis {

// The desktop window, single-instance (launching Synaxis again raises the
// existing window): the web UI in a WebKitGTK view on Linux
// (app_window.cpp), in Edge WebView2 on Windows (app_window_win.cpp).
class AppWindow {
public:
    // Run()'s result when no window can be shown (WebView2 isn't installed):
    // the caller falls back to the web browser.
    static constexpr int kUnavailable = 125;

    // Registers with the session bus to find out whether another Synaxis
    // window already exists.
    explicit AppWindow(std::filesystem::path state_file);
    ~AppWindow();

    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    // True when another process owns the window; Run() then just raises it.
    bool remote() const;

    // Shows `url` and blocks until the window is closed (or, when remote,
    // asks the other instance to show itself and returns at once).
    int Run(const std::string& url);

    // Closes the window from any thread.
    void Quit();

private:
#ifdef _WIN32
    void* mutex_ = nullptr;  // HANDLE; held while this process owns the window
    std::atomic<void*> view_ = nullptr;  // webview_t, while Run() shows it
    std::atomic<bool> quit_ = false;     // Quit() came before the window
    bool remote_ = false;
#else
    static void OnActivate(GtkApplication* app, void* self);
    void BuildWindow();
    void SaveGeometry();

    GtkApplication* app_ = nullptr;
    GtkWidget* window_ = nullptr;
    GtkWidget* view_ = nullptr;
    bool fullscreen_ = false;
    bool maximized_ = false;
#endif
    std::filesystem::path state_file_;  // remembered window size
    std::string url_;
};

} // namespace synaxis
