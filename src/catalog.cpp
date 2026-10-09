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

#include "synaxis/catalog.hpp"

#include "synaxis/media_library.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <regex>
#include <set>

#include <nlohmann/json.hpp>

namespace synaxis {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::uint64_t Fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string Hex16(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

bool HasVideoExtension(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext.size() < 2) return false;
    ext.erase(0, 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    const auto& known = MediaLibrary::DefaultVideoExtensions();
    return std::find(known.begin(), known.end(), ext) != known.end();
}

std::int64_t UnixSeconds(fs::file_time_type t) {
    auto sys = std::chrono::file_clock::to_sys(t);
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

// Case/punctuation-insensitive grouping key: "Rick & Morty" and
// "rick.and.morty" land in the same show.
std::string NormalizeKey(const std::string& name) {
    std::string out;
    bool space = false;
    for (unsigned char c : name) {
        if (c == '&') {
            out += out.empty() || space ? "and " : " and ";
            space = true;
        } else if (c == '\'') {
            continue;
        } else if (std::isalnum(c)) {
            out += char(std::tolower(c));
            space = false;
        } else if (!out.empty() && !space) {
            out += ' ';
            space = true;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string TitleCase(std::string s) {
    bool start = true;
    for (char& c : s) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            if (start) c = char(std::toupper(static_cast<unsigned char>(c)));
            start = false;
        } else {
            start = c == ' ' || c == '-';
        }
    }
    return s;
}

// Name used to group a file when the filename has no usable title, e.g.
// "S01E01.mkv" inside "Show Name/Season 1/".
std::string FallbackName(const VideoFile& file) {
    static const std::regex kSeasonDir(R"(^(season|series|s)[\s._-]*\d+$)", std::regex::icase);
    if (file.metadata.season) {
        fs::path dir = file.path.parent_path();
        if (std::regex_match(dir.filename().string(), kSeasonDir)) dir = dir.parent_path();
        if (!dir.filename().empty()) return dir.filename().string();
    }
    return file.path.stem().string();
}

int Height(const VideoFile& f) {
    return f.info ? f.info->height : 0;
}

// Best copy first: highest resolution, then largest (least compressed).
bool BetterCopy(const VideoFile& a, const VideoFile& b) {
    if (Height(a) != Height(b)) return Height(a) > Height(b);
    if (a.size != b.size) return a.size > b.size;
    return a.path < b.path;
}

} // namespace

std::string FileId(const fs::path& path) {
    return Hex16(Fnv1a(path.string()));
}

std::vector<VideoFile> ScanVideoFiles(const ScanOptions& options, const std::atomic<bool>* cancel) {
    std::vector<VideoFile> files;
    std::set<fs::path> seen;

    for (const fs::path& root : options.roots) {
        std::vector<fs::path> pending{root};
        while (!pending.empty()) {
            if (cancel && cancel->load()) return files;
            fs::path dir = std::move(pending.back());
            pending.pop_back();

            std::error_code ec;
            fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
            for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
                const fs::directory_entry& entry = *it;
                const std::string name = entry.path().filename().string();
                if (name.empty() || name.front() == '.') continue;

                std::error_code entry_ec;
                if (entry.is_directory(entry_ec)) {
                    if (!entry.is_symlink(entry_ec)) pending.push_back(entry.path());
                    continue;
                }
                if (!entry.is_regular_file(entry_ec) || !HasVideoExtension(entry.path())) continue;

                std::uintmax_t size = entry.file_size(entry_ec);
                if (entry_ec || size < options.min_size) continue;
                fs::path canonical = fs::canonical(entry.path(), entry_ec);
                if (entry_ec || !seen.insert(canonical).second) continue;

                VideoFile file;
                file.id = FileId(canonical);
                file.path = canonical;
                file.size = size;
                file.mtime = UnixSeconds(entry.last_write_time(entry_ec));
                file.metadata = FilenameParser::parse(canonical.filename().string());
                files.push_back(std::move(file));
            }
        }
    }

    std::sort(files.begin(), files.end(),
              [](const VideoFile& a, const VideoFile& b) { return a.path < b.path; });
    return files;
}

void SaveVideoFiles(const std::vector<VideoFile>& files, const fs::path& path) {
    json arr = json::array();
    for (const VideoFile& f : files) {
        json info = nullptr;
        if (f.info) {
            info = {{"duration", f.info->duration},
                    {"width", f.info->width},
                    {"height", f.info->height},
                    {"codec", f.info->video_codec},
                    {"hdr", f.info->hdr}};
        }
        arr.push_back({{"path", f.path.string()},
                       {"size", f.size},
                       {"mtime", f.mtime},
                       {"probed", f.probed},
                       {"info", info}});
    }

    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << json{{"version", 1}, {"files", arr}}.dump();
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
}

std::vector<VideoFile> LoadVideoFiles(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    json root = json::parse(in, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object() || !root.contains("files")) return {};

    std::vector<VideoFile> files;
    try {
        for (const json& j : root.at("files")) {
            VideoFile f;
            f.path = j.at("path").get<std::string>();
            f.id = FileId(f.path);
            f.size = j.at("size").get<std::uintmax_t>();
            f.mtime = j.at("mtime").get<std::int64_t>();
            f.probed = j.value("probed", false);
            f.metadata = FilenameParser::parse(f.path.filename().string());
            if (const json& info = j.at("info"); !info.is_null()) {
                f.info = MediaInfo{info.at("duration").get<double>(),
                                   info.at("width").get<int>(),
                                   info.at("height").get<int>(),
                                   info.at("codec").get<std::string>(),
                                   info.at("hdr").get<bool>()};
            }
            files.push_back(std::move(f));
        }
    } catch (const json::exception&) {
        return {};  // corrupt cache: rescan from scratch
    }
    return files;
}

Catalog::Catalog(std::vector<VideoFile> files) : files_(std::move(files)) {
    for (size_t i = 0; i < files_.size(); ++i) file_index_[files_[i].id] = i;

    struct Group {
        TitleKind kind;
        std::optional<int> year;
        std::vector<size_t> files;
        std::map<std::string, int> names;  // spelling -> occurrences
    };
    std::map<std::string, Group> groups;  // ordered: deterministic title order

    for (size_t i = 0; i < files_.size(); ++i) {
        const VideoFile& f = files_[i];
        const ParsedFilename& m = f.metadata;
        bool episode = m.season && m.episode;
        std::string name = m.title ? *m.title : FallbackName(f);
        std::string key = NormalizeKey(name);
        if (key.empty()) {
            name = f.path.stem().string();
            key = NormalizeKey(name);
        }
        key = (episode ? "show:" : "movie:") + key;
        if (!episode && m.year) key += "|" + std::to_string(*m.year);

        Group& g = groups.try_emplace(key, Group{episode ? TitleKind::kShow : TitleKind::kMovie})
                       .first->second;
        g.files.push_back(i);
        g.names[name]++;
        if (m.year && (!g.year || *m.year < *g.year)) g.year = m.year;
    }

    auto better = [this](const std::string& a, const std::string& b) {
        return BetterCopy(files_[file_index_.at(a)], files_[file_index_.at(b)]);
    };

    for (auto& [key, g] : groups) {
        Title t;
        t.id = Hex16(Fnv1a(key));
        t.kind = g.kind;
        t.year = g.year;

        // Display name: the most common spelling, preferring ones with
        // capitals ("Mad Max" over "mad max"); title-case if all lowercase.
        int best_votes = -1;
        bool best_has_upper = false;
        for (const auto& [spelling, votes] : g.names) {
            bool has_upper = std::any_of(spelling.begin(), spelling.end(),
                                         [](unsigned char c) { return std::isupper(c); });
            if (std::tie(has_upper, votes) > std::tie(best_has_upper, best_votes)) {
                t.name = spelling;
                best_votes = votes;
                best_has_upper = has_upper;
            }
        }
        if (!best_has_upper) t.name = TitleCase(t.name);

        if (g.kind == TitleKind::kMovie) {
            TitleItem item;
            for (size_t i : g.files) item.file_ids.push_back(files_[i].id);
            t.items.push_back(std::move(item));
        } else {
            // Specials (season 0) go after the regular seasons.
            auto order = [](int season, int episode) {
                return std::pair(season == 0 ? INT32_MAX : season, episode);
            };
            std::map<std::pair<int, int>, TitleItem> episodes;
            for (size_t i : g.files) {
                const ParsedFilename& m = files_[i].metadata;
                TitleItem& item = episodes[order(*m.season, *m.episode)];
                item.season = m.season;
                item.episode = m.episode;
                item.file_ids.push_back(files_[i].id);
            }
            for (auto& [_, item] : episodes) t.items.push_back(std::move(item));
        }

        for (TitleItem& item : t.items) {
            std::sort(item.file_ids.begin(), item.file_ids.end(), better);
        }
        for (size_t i : g.files) t.added = std::max(t.added, files_[i].mtime);
        titles_.push_back(std::move(t));
    }

    std::sort(titles_.begin(), titles_.end(), [](const Title& a, const Title& b) {
        return NormalizeKey(a.name) < NormalizeKey(b.name);
    });
    for (size_t t = 0; t < titles_.size(); ++t) {
        title_index_[titles_[t].id] = t;
        for (size_t i = 0; i < titles_[t].items.size(); ++i) {
            for (const std::string& id : titles_[t].items[i].file_ids) file_location_[id] = {t, i};
        }
    }
}

const VideoFile* Catalog::FindFile(const std::string& id) const {
    auto it = file_index_.find(id);
    return it == file_index_.end() ? nullptr : &files_[it->second];
}

const Title* Catalog::FindTitle(const std::string& id) const {
    auto it = title_index_.find(id);
    return it == title_index_.end() ? nullptr : &titles_[it->second];
}

std::optional<std::pair<const Title*, size_t>> Catalog::Locate(const std::string& file_id) const {
    auto it = file_location_.find(file_id);
    if (it == file_location_.end()) return std::nullopt;
    return std::pair(&titles_[it->second.first], it->second.second);
}

} // namespace synaxis
