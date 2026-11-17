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

// The few things that differ between Linux and Windows, behind one face.
//
// Strings in Synaxis are UTF-8 everywhere (JSON, FFmpeg, VLC's command
// line). Paths must be converted with PathToUtf8()/PathFromUtf8() rather
// than path.string() and path(std::string): on Windows those use the
// system's legacy code page and mangle names like "Amélie.mkv".

#include <filesystem>
#include <optional>
#include <string>

namespace synaxis {

std::string PathToUtf8(const std::filesystem::path& path);
std::filesystem::path PathFromUtf8(const std::string& utf8);

// An environment variable, as UTF-8; nullopt if unset or empty.
std::optional<std::string> GetEnv(const char* name);

// The user's home folder ($HOME, or %USERPROFILE% on Windows).
std::filesystem::path HomeDir();

// "~" and "~/..." (or "~\..." on Windows) become the home folder.
std::string ExpandHome(const std::string& path);

// Drops trailing separators, but keeps a root ("/", "C:\") whole.
std::string TrimTrailingSeparators(std::string path);

// Folders the scan skips: dot-folders everywhere, plus folders Windows marks
// hidden or system (AppData, $Recycle.Bin, ...).
bool IsHiddenEntry(const std::filesystem::directory_entry& entry);

// An unused TCP port on 127.0.0.1, or 0. There's a small window before the
// caller binds it, which is fine for a desktop app.
int FreeLocalPort();

// The login name, for the first profile; "" if unknown.
std::string UserName();

} // namespace synaxis
