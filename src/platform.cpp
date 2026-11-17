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

#include "synaxis/platform.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstdlib>

namespace synaxis {

namespace fs = std::filesystem;

std::string PathToUtf8(const fs::path& path) {
    std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

fs::path PathFromUtf8(const std::string& utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::optional<std::string> GetEnv(const char* name) {
#ifdef _WIN32
    // _wgetenv, so values outside the legacy code page survive.
    std::wstring wname(name, name + std::char_traits<char>::length(name));
    const wchar_t* value = _wgetenv(wname.c_str());
    if (!value || !*value) return std::nullopt;
    return PathToUtf8(fs::path(value));
#else
    const char* value = std::getenv(name);
    if (!value || !*value) return std::nullopt;
    return std::string(value);
#endif
}

fs::path HomeDir() {
#ifdef _WIN32
    if (auto home = GetEnv("USERPROFILE")) return PathFromUtf8(*home);
#else
    if (auto home = GetEnv("HOME")) return PathFromUtf8(*home);
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

std::string ExpandHome(const std::string& path) {
    bool tilde = path == "~" || path.rfind("~/", 0) == 0;
#ifdef _WIN32
    tilde = tilde || path.rfind("~\\", 0) == 0;
#endif
    if (!tilde) return path;
    return PathToUtf8(HomeDir()) + path.substr(1);
}

std::string TrimTrailingSeparators(std::string path) {
    auto separator = [](char c) {
#ifdef _WIN32
        return c == '/' || c == '\\';
#else
        return c == '/';
#endif
    };
    while (path.size() > 1 && separator(path.back())) {
#ifdef _WIN32
        if (path.size() == 3 && path[1] == ':') break;  // "C:\"
#endif
        path.pop_back();
    }
    return path;
}

bool IsHiddenEntry(const fs::directory_entry& entry) {
    std::string name = PathToUtf8(entry.path().filename());
    if (name.empty() || name.front() == '.') return true;
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW(entry.path().c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) {
        return true;
    }
#endif
    return false;
}

int FreeLocalPort() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!started) return 0;
    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return 0;
    using Length = int;
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    using Length = socklen_t;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    Length len = sizeof addr;
    int port = 0;
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0 &&
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        port = ntohs(addr.sin_port);
    }
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return port;
}

std::string UserName() {
#ifdef _WIN32
    return GetEnv("USERNAME").value_or("");
#else
    return GetEnv("USER").value_or("");
#endif
}

} // namespace synaxis
