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

// The Windows window: the web UI in Edge WebView2, through the webview
// library (https://github.com/webview/webview). WebView2 comes with
// Windows 10 and 11; where it's missing, Run() says so and the caller opens
// the browser instead.

#include "app_window.hpp"

#include "synaxis/platform.hpp"

#include <windows.h>

#include <webview/webview.h>

#include <fstream>

namespace synaxis {

namespace {

constexpr wchar_t kTitle[] = L"Synaxis";
constexpr wchar_t kMutexName[] = L"Local\\io.synaxis.Synaxis.window";
constexpr int kDefaultWidth = 1360;
constexpr int kDefaultHeight = 860;
constexpr int kIconResource = 1;  // packaging/windows/synaxis.rc

// Keys the Linux window handles itself; here the page passes them on.
constexpr char kKeysScript[] = R"js(
  window.addEventListener('keydown', (e) => {
    if (e.key === 'F11') { e.preventDefault(); window.synaxisFullscreen(); }
    else if (e.ctrlKey && (e.key === 'q' || e.key === 'Q')) { e.preventDefault(); window.synaxisQuit(); }
  }, true);
)js";

// Window state, for the window procedure we put in front of webview's.
HWND g_window = nullptr;
WNDPROC g_original_proc = nullptr;
std::filesystem::path g_state_file;
bool g_fullscreen = false;
WINDOWPLACEMENT g_before_fullscreen{sizeof(WINDOWPLACEMENT)};
LONG_PTR g_style_before_fullscreen = 0;

void SavePlacement() {
    WINDOWPLACEMENT placement{sizeof placement};
    if (g_fullscreen) {
        placement = g_before_fullscreen;
    } else if (!GetWindowPlacement(g_window, &placement)) {
        return;
    }
    const RECT& r = placement.rcNormalPosition;
    std::ofstream out(g_state_file, std::ios::trunc);
    out << r.left << ' ' << r.top << ' ' << r.right << ' ' << r.bottom << ' '
        << (placement.showCmd == SW_SHOWMAXIMIZED ? 1 : 0) << '\n';
}

void RestorePlacement() {
    std::ifstream in(g_state_file);
    RECT r{};
    int maximized = 0;
    if (!(in >> r.left >> r.top >> r.right >> r.bottom >> maximized)) return;
    if (r.right - r.left < 400 || r.bottom - r.top < 300) return;
    // Not if that spot is off every screen now (a monitor was unplugged).
    if (!MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) return;
    WINDOWPLACEMENT placement{sizeof placement};
    placement.rcNormalPosition = r;
    placement.showCmd = maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    SetWindowPlacement(g_window, &placement);
}

void ToggleFullscreen() {
    if (!g_fullscreen) {
        g_before_fullscreen.length = sizeof g_before_fullscreen;
        GetWindowPlacement(g_window, &g_before_fullscreen);
        g_style_before_fullscreen = GetWindowLongPtrW(g_window, GWL_STYLE);
        MONITORINFO monitor{sizeof monitor};
        GetMonitorInfoW(MonitorFromWindow(g_window, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(g_window, GWL_STYLE, g_style_before_fullscreen & ~WS_OVERLAPPEDWINDOW);
        const RECT& m = monitor.rcMonitor;
        SetWindowPos(g_window, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = true;
    } else {
        SetWindowLongPtrW(g_window, GWL_STYLE, g_style_before_fullscreen);
        SetWindowPlacement(g_window, &g_before_fullscreen);
        SetWindowPos(g_window, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_CLOSE) SavePlacement();  // before webview destroys the window
    return CallWindowProcW(g_original_proc, hwnd, message, wparam, lparam);
}

void OnFullscreen(const char* id, const char*, void* view) {
    ToggleFullscreen();
    webview_return(static_cast<webview_t>(view), id, 0, "");
}

void OnQuit(const char* id, const char*, void* view) {
    webview_return(static_cast<webview_t>(view), id, 0, "");
    PostMessageW(g_window, WM_CLOSE, 0, 0);
}

} // namespace

AppWindow::AppWindow(std::filesystem::path state_file) : state_file_(std::move(state_file)) {
    // Held for as long as this process lives; a second launch sees it taken.
    mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
    remote_ = mutex_ && GetLastError() == ERROR_ALREADY_EXISTS;
}

AppWindow::~AppWindow() {
    if (mutex_) CloseHandle(mutex_);
}

bool AppWindow::remote() const {
    return remote_;
}

int AppWindow::Run(const std::string& url) {
    if (remote_) {
        // Bring the other instance's window to the front.
        if (HWND other = FindWindowW(nullptr, kTitle)) {
            if (IsIconic(other)) ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }
    if (quit_) return 0;

    // WebView2 keeps its cache and cookies here, beside our own data,
    // rather than next to the .exe (which may be read-only).
    std::filesystem::path data = state_file_.parent_path() / "WebView2";
    SetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", data.c_str());

    bool debug = GetEnv("SYNAXIS_DEBUG").has_value();
    webview_t view = webview_create(debug ? 1 : 0, nullptr);
    if (!view) return kUnavailable;

    g_window = static_cast<HWND>(webview_get_window(view));
    g_state_file = state_file_;
    SetWindowTextW(g_window, kTitle);
    webview_set_size(view, kDefaultWidth, kDefaultHeight, WEBVIEW_HINT_NONE);
    RestorePlacement();
    HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource));
    if (icon) {
        SendMessageW(g_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SendMessageW(g_window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
    }
    g_original_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));

    webview_bind(view, "synaxisFullscreen", OnFullscreen, view);
    webview_bind(view, "synaxisQuit", OnQuit, view);
    webview_init(view, kKeysScript);
    webview_navigate(view, url.c_str());

    view_ = view;
    if (!quit_) webview_run(view);
    view_ = nullptr;
    webview_destroy(view);
    g_window = nullptr;
    return 0;
}

void AppWindow::Quit() {
    quit_ = true;
    if (void* view = view_.load()) {
        // Only the window's own thread may close it.
        webview_dispatch(
            static_cast<webview_t>(view),
            [](webview_t, void*) {
                if (g_window) PostMessageW(g_window, WM_CLOSE, 0, 0);
            },
            nullptr);
    }
}

} // namespace synaxis
