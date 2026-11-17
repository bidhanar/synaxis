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

#include "synaxis/process.hpp"

#include "synaxis/platform.hpp"

#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstring>

extern char** environ;
#endif

namespace synaxis {

#ifdef _WIN32

namespace {

std::wstring Wide(const std::string& utf8) {
    return PathFromUtf8(utf8).wstring();
}

std::string LastErrorMessage() {
    DWORD code = GetLastError();
    wchar_t* text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string message = text ? PathToUtf8(std::filesystem::path(text)) : "error " + std::to_string(code);
    LocalFree(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == '.')) {
        message.pop_back();
    }
    return message;
}

} // namespace

std::wstring QuoteWindowsArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++backslashes;
            continue;
        }
        // Backslashes are literal unless they come before a quote.
        out.append(c == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        backslashes = 0;
        out += c;
    }
    out.append(backslashes * 2, L'\\');  // before the closing quote
    out += L'"';
    return out;
}

std::unique_ptr<Process> Process::Spawn(const std::vector<std::string>& argv, bool detached,
                                        std::string* error) {
    if (argv.empty()) {
        if (error) *error = "empty command";
        return nullptr;
    }
    std::wstring command;
    for (const std::string& arg : argv) {
        if (!command.empty()) command += L' ';
        command += QuoteWindowsArgument(Wide(arg));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION info{};
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW;
    if (detached) flags |= CREATE_NEW_PROCESS_GROUP;
    // CreateProcessW may write to the command line, so it gets its own copy.
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr,
                        &startup, &info)) {
        if (error) *error = "could not start " + argv[0] + ": " + LastErrorMessage();
        return nullptr;
    }
    CloseHandle(info.hThread);
    std::unique_ptr<Process> process(new Process());
    process->handle_ = info.hProcess;
    return process;
}

Process::~Process() {
    if (handle_) CloseHandle(handle_);
}

bool Process::Running() {
    if (exited_) return false;
    if (WaitForSingleObject(handle_, 0) == WAIT_OBJECT_0) {
        DWORD code = 0;
        if (GetExitCodeProcess(handle_, &code)) exit_code_ = int(code);
        exited_ = true;
    }
    return !exited_;
}

void Process::Terminate() {
    if (!Running()) return;
    // VLC has no polite "quit" we can send from outside; this is what
    // closing it from Task Manager does.
    TerminateProcess(handle_, 1);
    WaitForSingleObject(handle_, 1000);
    Running();
}

std::optional<int> Process::Wait() {
    WaitForSingleObject(handle_, INFINITE);
    Running();
    return exit_code_;
}

#else  // POSIX

std::unique_ptr<Process> Process::Spawn(const std::vector<std::string>& argv, bool detached,
                                        std::string* error) {
    if (argv.empty()) {
        if (error) *error = "empty command";
        return nullptr;
    }

    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclosefrom_np(&actions, 3);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    if (detached) posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    pid_t pid = -1;
    int rc = posix_spawnp(&pid, args[0], &actions, &attr, args.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);

    if (rc != 0) {
        if (error) *error = "could not start " + argv[0] + ": " + std::strerror(rc);
        return nullptr;
    }
    std::unique_ptr<Process> process(new Process());
    process->pid_ = pid;
    return process;
}

Process::~Process() {
    Running();  // reap it if it already exited, so it doesn't linger as a zombie
}

bool Process::Running() {
    if (exited_ || pid_ < 0) return false;
    int status = 0;
    pid_t r = waitpid(pid_, &status, WNOHANG);
    if (r == pid_) {
        if (WIFEXITED(status)) exit_code_ = WEXITSTATUS(status);
        exited_ = true;
    } else if (r < 0 && errno == ECHILD) {
        exited_ = true;
    }
    return !exited_;
}

void Process::Terminate() {
    if (!Running()) return;
    kill(pid_, SIGTERM);
    for (int i = 0; i < 20 && Running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

std::optional<int> Process::Wait() {
    if (exited_ || pid_ < 0) return exit_code_;
    int status = 0;
    while (waitpid(pid_, &status, 0) < 0) {
        if (errno != EINTR) {
            exited_ = true;
            return exit_code_;
        }
    }
    if (WIFEXITED(status)) exit_code_ = WEXITSTATUS(status);
    exited_ = true;
    return exit_code_;
}

#endif

} // namespace synaxis
