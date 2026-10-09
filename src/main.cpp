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

#include "synaxis/media_library.hpp"
#include "synaxis/player.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kLibraryPath = "library.json";

#ifndef SYNAXIS_VERSION
#define SYNAXIS_VERSION "unknown"
#endif

// Standard GNU-style --version notice (see the GNU Coding Standards,
// "--version"): this is the non-interactive equivalent of the GPL's
// suggested interactive `show w' / `show c' commands — it folds the
// warranty disclaimer and redistribution notice into one message instead of
// requiring a second command to see them, since a CLI has no session to
// type a follow-up command into.
void PrintVersion() {
    std::cout << "Synaxis " << SYNAXIS_VERSION << "\n"
               << "Copyright (C) 2026 Azhar Tanweer\n"
               << "License GPLv3+: GNU GPL version 3 or later "
                  "<https://gnu.org/licenses/gpl.html>\n"
               << "This is free software: you are free to change and redistribute it.\n"
               << "There is NO WARRANTY, to the extent permitted by law.\n"
                  "See the LICENSE file distributed with this program for the full text.\n";
}

void PrintParsed(const fs::path& path, const synaxis::ParsedFilename& parsed) {
    std::cout << path.string() << "\n";
    std::cout << "  title:   " << (parsed.title ? *parsed.title : "-") << "\n";
    std::cout << "  year:    " << (parsed.year ? std::to_string(*parsed.year) : "-") << "\n";
    std::cout << "  season:  " << (parsed.season ? std::to_string(*parsed.season) : "-") << "\n";
    std::cout << "  episode: " << (parsed.episode ? std::to_string(*parsed.episode) : "-") << "\n";
    std::cout << "  ep name: " << (parsed.episode_title ? *parsed.episode_title : "-") << "\n";
    std::cout << "  source:  " << (parsed.source ? *parsed.source : "-") << "\n";
    std::cout << "  tags:    ";
    if (parsed.tags.empty()) {
        std::cout << "-";
    } else {
        for (size_t i = 0; i < parsed.tags.size(); ++i) {
            if (i) std::cout << ", ";
            std::cout << parsed.tags[i];
        }
    }
    std::cout << "\n\n";
}

void PrintUsage(const char* argv0) {
    std::cerr << "usage:\n"
              << "  " << argv0 << " --version\n"
              << "      print version, copyright, and license notice\n"
              << "  " << argv0 << " -d <directory> [-x <ext1,ext2,...>]\n"
              << "      scan <directory> and (re)build " << kLibraryPath << "\n"
              << "      -x restricts scanning to the given extensions, a subset of the\n"
              << "         built-in defaults (comma-separated, no leading dot)\n"
              << "  " << argv0 << " -p -f <series name> -s <season> -e <episode>\n"
              << "      play a TV episode\n"
              << "  " << argv0 << " -p -f <movie name> [-y <year>]\n"
              << "      play a movie\n";
}

struct Args {
    std::optional<std::string> directory;
    bool play = false;
    std::optional<std::string> name;
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<int> year;
    std::optional<std::vector<std::string>> extensions;
};

// Splits a comma-separated list, trimming whitespace and any leading dot
// from each entry (so "-x .mp4, mkv" and "-x mp4,mkv" behave the same).
std::vector<std::string> SplitExtensionList(const std::string& raw) {
    std::vector<std::string> result;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t comma = raw.find(',', start);
        std::string token = raw.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

        size_t begin = token.find_first_not_of(" \t");
        size_t end = token.find_last_not_of(" \t");
        if (begin != std::string::npos) {
            token = token.substr(begin, end - begin + 1);
            if (!token.empty() && token.front() == '.') token.erase(token.begin());
            if (!token.empty()) result.push_back(token);
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

// Returns nullopt (and prints an error) on a malformed command line.
std::optional<Args> ParseArgs(int argc, char** argv) {
    Args args;

    auto next_value = [&](int& i) -> std::optional<std::string> {
        if (i + 1 >= argc) return std::nullopt;
        return std::string(argv[++i]);
    };

    for (int i = 1; i < argc; ++i) {
        std::string flag = argv[i];
        if (flag == "-d") {
            auto v = next_value(i);
            if (!v) { std::cerr << "error: -d requires a directory\n"; return std::nullopt; }
            args.directory = *v;
        } else if (flag == "-p") {
            args.play = true;
        } else if (flag == "-f") {
            auto v = next_value(i);
            if (!v) { std::cerr << "error: -f requires a name\n"; return std::nullopt; }
            args.name = *v;
        } else if (flag == "-s") {
            auto v = next_value(i);
            if (!v) { std::cerr << "error: -s requires a season number\n"; return std::nullopt; }
            args.season = std::stoi(*v);
        } else if (flag == "-e") {
            auto v = next_value(i);
            if (!v) { std::cerr << "error: -e requires an episode number\n"; return std::nullopt; }
            args.episode = std::stoi(*v);
        } else if (flag == "-y") {
            auto v = next_value(i);
            if (!v) { std::cerr << "error: -y requires a year\n"; return std::nullopt; }
            args.year = std::stoi(*v);
        } else if (flag == "-x") {
            auto v = next_value(i);
            if (!v) { std::cerr << "error: -x requires a comma-separated extension list\n"; return std::nullopt; }
            args.extensions = SplitExtensionList(*v);
        } else {
            std::cerr << "error: unrecognized argument: " << flag << "\n";
            return std::nullopt;
        }
    }

    return args;
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
        return std::tolower(x) == std::tolower(y);
    });
}

void PrintCandidate(size_t index, const synaxis::MediaEntry& entry) {
    const auto& m = entry.metadata;
    std::cout << "  [" << index << "] " << (m.title ? *m.title : "?");
    if (m.season && m.episode) {
        std::cout << " S" << *m.season << "E" << *m.episode;
    }
    if (m.year) std::cout << " (" << *m.year << ")";
    if (m.source) std::cout << " [" << *m.source << "]";
    std::cout << "\n      " << entry.path.string() << "\n";
}

// Prompts the user to pick one of several ambiguous candidates.
std::optional<synaxis::MediaEntry> PromptForChoice(
    const std::vector<synaxis::MediaEntry>& candidates) {
    std::cout << "multiple matches found:\n";
    for (size_t i = 0; i < candidates.size(); ++i) PrintCandidate(i, candidates[i]);

    while (true) {
        std::cout << "choose [0-" << candidates.size() - 1 << "]: ";
        std::string line;
        if (!std::getline(std::cin, line)) return std::nullopt;

        try {
            size_t idx = std::stoul(line);
            if (idx < candidates.size()) return candidates[idx];
        } catch (const std::exception&) {
            // fall through to re-prompt
        }
        std::cout << "invalid choice, try again.\n";
    }
}

// Prints the list of extensions Scan() will accept, comma-separated.
std::string JoinExtensions(const std::vector<std::string>& extensions) {
    std::string joined;
    for (size_t i = 0; i < extensions.size(); ++i) {
        if (i) joined += ", ";
        joined += extensions[i];
    }
    return joined;
}

int RunScan(const std::string& directory, const std::optional<std::vector<std::string>>& extensions) {
    fs::path root(directory);
    if (!fs::is_directory(root)) {
        std::cerr << "error: not a directory: " << root << "\n";
        return 1;
    }

    const auto& defaults = synaxis::MediaLibrary::DefaultVideoExtensions();
    if (extensions) {
        for (const auto& ext : *extensions) {
            bool known = std::any_of(defaults.begin(), defaults.end(),
                                      [&](const std::string& d) { return EqualsIgnoreCase(d, ext); });
            if (!known) {
                std::cerr << "error: unknown extension \"" << ext << "\". Available extensions: "
                           << JoinExtensions(defaults) << "\n";
                return 1;
            }
        }
    }

    std::vector<synaxis::MediaEntry> entries =
        synaxis::MediaLibrary::Scan(root, extensions.value_or(std::vector<std::string>{}));
    for (const auto& entry : entries) {
        PrintParsed(entry.path, entry.metadata);
    }

    synaxis::MediaLibrary::SaveToJson(entries, kLibraryPath);
    std::cout << entries.size() << " files indexed -> " << kLibraryPath << "\n";
    return 0;
}

int RunPlay(const Args& args) {
    if (!args.name) {
        std::cerr << "error: -p requires -f <name>\n";
        return 1;
    }
    if (args.season.has_value() != args.episode.has_value()) {
        std::cerr << "error: -s and -e must be given together\n";
        return 1;
    }

    if (!fs::exists(kLibraryPath)) {
        std::cerr << "error: " << kLibraryPath << " not found — run '-d <directory>' first "
                                                    "to build the library.\n";
        return 1;
    }

    std::vector<synaxis::MediaEntry> library = synaxis::MediaLibrary::LoadFromJson(kLibraryPath);

    std::vector<synaxis::MediaEntry> candidates;
    for (const auto& entry : library) {
        const auto& m = entry.metadata;
        if (!m.title || !EqualsIgnoreCase(*m.title, *args.name)) continue;

        if (args.season && args.episode) {
            // TV lookup: season/episode must match exactly.
            if (m.season != *args.season || m.episode != *args.episode) continue;
        } else {
            // Movie lookup: exclude anything that looks like a TV episode,
            // and narrow by year if the user gave one.
            if (m.season || m.episode) continue;
            if (args.year && m.year != *args.year) continue;
        }

        candidates.push_back(entry);
    }

    if (candidates.empty()) {
        std::cerr << "error: no match found for \"" << *args.name << "\" in " << kLibraryPath
                   << ". Check the name/season/episode/year, or re-run '-d <directory>' if the "
                      "library is out of date.\n";
        return 1;
    }

    synaxis::MediaEntry chosen;
    if (candidates.size() == 1) {
        chosen = candidates.front();
    } else {
        // Same name, still ambiguous (e.g. two movies sharing a title) —
        // defer to the user to pick, showing year/season/episode as the
        // distinguishing details.
        auto picked = PromptForChoice(candidates);
        if (!picked) {
            std::cerr << "error: no selection made\n";
            return 1;
        }
        chosen = *picked;
    }

    if (!fs::exists(chosen.path)) {
        std::cerr << "error: file no longer exists on disk: " << chosen.path.string()
                   << ". Re-run '-d <directory>' to refresh " << kLibraryPath << ".\n";
        return 1;
    }

    if (!synaxis::Player::Play(chosen.path)) {
        std::cerr << "error: playback failed for " << chosen.path.string() << "\n";
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Handled before normal parsing/validation, like GNU tools' --version:
    // it's an informational query, not something that combines with -d/-p.
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--version") {
            PrintVersion();
            return 0;
        }
    }

    std::optional<Args> parsed = ParseArgs(argc, argv);
    if (!parsed) {
        PrintUsage(argv[0]);
        return 1;
    }
    Args args = *parsed;

    if (args.directory && args.play) {
        std::cerr << "error: -d and -p cannot be combined\n";
        PrintUsage(argv[0]);
        return 1;
    }
    if (args.extensions && !args.directory) {
        std::cerr << "error: -x requires -d\n";
        PrintUsage(argv[0]);
        return 1;
    }

    if (args.directory) return RunScan(*args.directory, args.extensions);
    if (args.play) return RunPlay(args);

    PrintUsage(argv[0]);
    return 1;
}
