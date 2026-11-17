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
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace synaxis {

// A program we started (VLC, the web browser).
class Process {
public:
    // Starts argv[0], looked up on the PATH, with UTF-8 arguments and its
    // standard input and output closed. A `detached` process runs in a
    // session (process group) of its own, so Ctrl+C in our terminal doesn't
    // reach it. Returns null and sets `error` if it can't be started.
    static std::unique_ptr<Process> Spawn(const std::vector<std::string>& argv, bool detached,
                                          std::string* error);
    ~Process();  // forgets the process; never stops it

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // False once it has exited. Safe to call from several threads.
    bool Running();
    // Stops it, waiting up to a second for it to go.
    void Terminate();
    // Waits for it to exit and returns its exit code (nullopt if unknown).
    std::optional<int> Wait();

private:
    Process() = default;

#ifdef _WIN32
    void* handle_ = nullptr;  // HANDLE
#else
    int pid_ = -1;
#endif
    std::atomic<bool> exited_ = false;
    std::optional<int> exit_code_;
};

#ifdef _WIN32
// One argument quoted for a Windows command line, the way programs built
// with Microsoft's C runtime (VLC among them) split it back up.
std::wstring QuoteWindowsArgument(const std::wstring& arg);
#endif

} // namespace synaxis
