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

// The Synaxis app: scans the library in the background and serves the
// browsing UI on localhost, shown in its own window (or the browser).

#include "server.hpp"
#ifdef SYNAXIS_HAS_WINDOW
#include "app_window.hpp"
#endif
#include "synaxis/process.hpp"

#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>

#include <cctype>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#ifndef SYNAXIS_VERSION
#define SYNAXIS_VERSION "unknown"
#endif

namespace fs = std::filesystem;

namespace {

// How the UI is shown.
enum class Mode {
    kWindow,    // Synaxis's own window (default when built with WebKitGTK)
    kBrowser,   // the default web browser
    kHeadless,  // nothing; just serve http://127.0.0.1:<port>/
};

#ifdef SYNAXIS_HAS_WINDOW
constexpr bool kHasWindow = true;
#else
constexpr bool kHasWindow = false;
#endif

void PrintUsage(const char* argv0) {
    std::cerr << "usage: " << argv0 << " [--port <port>] [--browser | --no-browser]\n"
              << "       " << argv0 << " --quit\n"
              << "  Opens your video library in its own window. --browser shows it in the\n"
              << "  web browser instead, --no-browser only serves http://127.0.0.1:<port>/\n"
              << "  (default 8484). --quit stops a running instance.\n"
              << "  Settings live in " << synaxis::DefaultAppPaths().config_file.string()
              << ".\n";
}

// Asks the instance on `port` to shut down. Returns whether one was running.
bool QuitRunning(int port) {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(0, 300'000);
    client.set_read_timeout(2, 0);
    httplib::Headers headers = {{"X-Synaxis", "1"}};
    auto res = client.Post("/api/quit", headers, "{}", "application/json");
    return res && res->status == 200;
}

// Snap apps (VS Code's terminal, for one) leak their private GTK/GLib paths
// to everything started from them. GTK then loads that snap's older
// settings schemas and modules, and aborts on the mismatch. Unless we're a
// snap ourselves, drop whatever points into a snap, so the window (and the
// VLC we launch) see the normal system setup.
void DropLeakedSnapEnvironment() {
    if (std::getenv("SNAP")) return;
    auto in_snap = [](const std::string& path) {
        return path.find("/snap/") != std::string::npos;
    };

    if (const char* dirs = std::getenv("XDG_DATA_DIRS")) {
        std::string kept, entry;
        std::istringstream in(dirs);
        while (std::getline(in, entry, ':')) {
            if (entry.empty() || in_snap(entry)) continue;
            kept += (kept.empty() ? "" : ":") + entry;
        }
        if (kept.empty()) kept = "/usr/local/share:/usr/share";
        setenv("XDG_DATA_DIRS", kept.c_str(), 1);
    }
    for (const char* name : {"GSETTINGS_SCHEMA_DIR", "GTK_PATH", "GTK_EXE_PREFIX", "GTK_IM_MODULE_FILE",
                             "GIO_MODULE_DIR", "GDK_PIXBUF_MODULE_FILE", "LOCPATH"}) {
        const char* value = std::getenv(name);
        if (value && in_snap(value)) unsetenv(name);
    }
}

std::string DefaultProfileName() {
    const char* user = std::getenv("USER");
    std::string name = user && *user ? user : "Me";
    name[0] = char(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

bool AlreadyRunning(int port) {
    httplib::Client client("127.0.0.1", port);
    client.set_connection_timeout(0, 300'000);
    client.set_read_timeout(1, 0);
    auto res = client.Get("/api/ping");
    return res && res->status == 200 && res->body.find("\"synaxis\"") != std::string::npos;
}

void OpenBrowser(const std::string& url) {
    std::string error;
    pid_t pid = synaxis::SpawnProcess({"xdg-open", url}, /*detach=*/true, &error);
    if (pid < 0) {
        std::cerr << "Couldn't open a browser (" << error << "). Visit " << url << "\n";
        return;
    }
    std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
}

} // namespace

int main(int argc, char** argv) {
    DropLeakedSnapEnvironment();

    int port = -1;
    Mode mode = kHasWindow ? Mode::kWindow : Mode::kBrowser;
    bool quit = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            try {
                port = std::stoi(argv[++i]);
            } catch (const std::exception&) {
                port = 0;
            }
            if (port <= 0 || port > 65535) {
                std::cerr << "error: --port needs a number between 1 and 65535\n";
                return 1;
            }
        } else if (arg == "--browser") {
            mode = Mode::kBrowser;
        } else if (arg == "--no-browser") {
            mode = Mode::kHeadless;
        } else if (arg == "--quit") {
            quit = true;
        } else if (arg == "--version") {
            std::cout << "Synaxis " << SYNAXIS_VERSION << "\n";
            return 0;
        } else {
            PrintUsage(argv[0]);
            return arg == "--help" || arg == "-h" ? 0 : 1;
        }
    }

    synaxis::AppPaths paths = synaxis::DefaultAppPaths();
    std::error_code ec;
    fs::create_directories(paths.data_dir, ec);
    synaxis::AppConfig config = synaxis::LoadConfig(paths.config_file);
    if (!fs::exists(paths.config_file, ec)) synaxis::SaveConfig(config, paths.config_file);
    if (port < 0) port = config.port;
    std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";

    if (quit) {
        bool was_running = QuitRunning(port);
        std::cout << (was_running ? "Synaxis stopped.\n" : "Synaxis isn't running.\n");
        return 0;
    }

#ifdef SYNAXIS_HAS_WINDOW
    std::optional<synaxis::AppWindow> window;
    if (mode == Mode::kWindow) {
        window.emplace(paths.data_dir / "window.ini");
        // Launched again from the menu: raise the window that's already open.
        if (window->remote()) return window->Run(url);
    }
#endif

    // A server is already up (e.g. started with --no-browser): just show it.
    if (AlreadyRunning(port)) {
#ifdef SYNAXIS_HAS_WINDOW
        if (window) return window->Run(url);
#endif
        std::cout << "Synaxis is already running at " << url << "\n";
        if (mode == Mode::kBrowser) OpenBrowser(url);
        return 0;
    }

    // Block SIGINT/SIGTERM before any thread starts, so only the waiter
    // below receives them and can shut the server down cleanly.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals, nullptr);

    synaxis::WatchState state(paths.data_dir / "watch_state.json");
    if (state.profiles().empty()) state.AddProfile(DefaultProfileName(), 0);
    synaxis::LibraryService library(paths.data_dir);
    synaxis::PlaybackManager playback(state);
    synaxis::Server server(paths, config, library, state, playback);

    if (!server.Bind(port)) {
        std::cerr << "error: can't listen on 127.0.0.1:" << port
                  << " (is something else using it? try --port)\n";
        return 1;
    }
    library.Rescan(config.scan_options());

    std::thread([&server, signals] {
        int sig = 0;
        sigwait(&signals, &sig);
        server.Stop();
    }).detach();

    std::cout << "Synaxis " << SYNAXIS_VERSION << " is running at " << url << "\n";

#ifdef SYNAXIS_HAS_WINDOW
    if (window) {
        // The server runs alongside the window. Whichever goes first takes
        // the other down: closing the window stops the server, and quitting
        // from the UI (or Ctrl+C) closes the window.
        std::thread serving([&] {
            server.Run();
            window->Quit();
        });
        server.WaitUntilReady();
        int status = window->Run(url);
        server.Stop();
        serving.join();
        return status;
    }
#endif

    std::cout << "Press Ctrl+C to quit.\n";
    if (mode == Mode::kBrowser) OpenBrowser(url);
    server.Run();
    return 0;
}
