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

#include "server.hpp"

#include "synaxis/feed.hpp"
#include "web_assets.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace synaxis {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using httplib::Request;
using httplib::Response;

// Episodes queued after the one picked, so VLC rolls on to the next like
// autoplay does. VLC stops at the end of the queue.
constexpr size_t kAutoplayQueue = 25;
constexpr size_t kRowLimit = 30;

std::int64_t UnixNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void SendJson(Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(j.dump(), "application/json");
}

void SendError(Response& res, int status, const std::string& message) {
    SendJson(res, {{"error", message}}, status);
}

std::optional<json> ParseBody(const Request& req) {
    json j = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) return std::nullopt;
    return j;
}

std::string StringField(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// Lowercase words separated by single spaces, for forgiving search.
std::string Simplify(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            out += char(std::tolower(c));
        } else if (!out.empty() && out.back() != ' ') {
            out += ' ';
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

const char* KindName(TitleKind kind) {
    return kind == TitleKind::kShow ? "show" : "movie";
}

const char* ReasonName(UpNext::Reason reason) {
    switch (reason) {
        case UpNext::Reason::kStart: return "start";
        case UpNext::Reason::kResume: return "resume";
        case UpNext::Reason::kNextEpisode: return "next";
        case UpNext::Reason::kRewatch: return "rewatch";
    }
    return "start";
}

std::string EpisodeLabel(const TitleItem& item) {
    if (!item.season || !item.episode) return "";
    return "S" + std::to_string(*item.season) + ":E" + std::to_string(*item.episode);
}

json Quality(const VideoFile* f) {
    if (!f || !f->info || f->info->height <= 0) return nullptr;
    if (f->info->height >= 1600) return "4K";
    if (f->info->height >= 700) return "HD";
    return "SD";
}

json OptionalString(const std::optional<std::string>& s) {
    return s ? json(*s) : json(nullptr);
}

const char* MimeFor(const fs::path& path) {
    std::string ext = path.extension().string();
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".svg") return "image/svg+xml";
    return "application/octet-stream";
}

// Serves a web UI file. SYNAXIS_WEB_DIR points at a web/ checkout to
// iterate on the UI without rebuilding; otherwise the compiled-in copy is used.
bool ServeAsset(const std::string& path, Response& res) {
    res.set_header("Cache-Control", "no-cache");
    if (const char* dir = std::getenv("SYNAXIS_WEB_DIR"); dir && *dir) {
        std::ifstream in(fs::path(dir) / path.substr(1), std::ios::binary);
        if (!in) return false;
        std::stringstream buf;
        buf << in.rdbuf();
        res.set_content(buf.str(), MimeFor(path));
        return true;
    }
    for (size_t i = 0; i < web::kAssetCount; ++i) {
        const web::Asset& asset = web::kAssets[i];
        if (path == asset.path) {
            res.set_content(reinterpret_cast<const char*>(asset.data), asset.size, asset.mime);
            return true;
        }
    }
    return false;
}

// Describes titles from one profile's point of view.
struct View {
    const Catalog& catalog;
    const Profile& profile;
    const LibraryService& library;

    json Image(const std::string& file_id) const {
        auto version = library.ThumbnailVersion(file_id);
        if (!version) return nullptr;
        return "/thumbs/" + file_id + ".jpg?v=" + std::to_string(*version);
    }

    // Title art: the first few episodes' (or the movie's) frames.
    json TitleImage(const Title& t) const {
        for (size_t i = 0; i < t.items.size() && i < 6; ++i) {
            for (const std::string& id : t.items[i].file_ids) {
                json image = Image(id);
                if (!image.is_null()) return image;
            }
        }
        return nullptr;
    }

    const VideoFile* Best(const TitleItem& item) const {
        return catalog.FindFile(item.file_ids.front());
    }

    json Card(const Title& t) const { return Card(t, ComputeUpNext(catalog, profile, t)); }

    json Card(const Title& t, const UpNext& up) const {
        const VideoFile* best = Best(t.items.front());
        json card = {
            {"id", t.id},
            {"name", t.name},
            {"kind", KindName(t.kind)},
            {"year", t.year ? json(*t.year) : json(nullptr)},
            {"image", TitleImage(t)},
            {"quality", Quality(best)},
            {"hdr", best && best->info && best->info->hdr},
            {"in_list", profile.my_list.count(t.id) > 0},
            {"added", t.added},
        };
        if (t.kind == TitleKind::kShow) {
            std::set<int> seasons;
            for (const TitleItem& item : t.items) seasons.insert(*item.season);
            card["seasons"] = seasons.size();
            card["episodes"] = t.items.size();
        } else {
            card["runtime"] = best && best->info ? best->info->duration : 0.0;
        }

        const VideoFile* up_file = catalog.FindFile(up.file_id);
        card["up_next"] = {
            {"file_id", up.file_id},
            {"label", EpisodeLabel(t.items[up.item])},
            {"episode_title", up_file ? OptionalString(up_file->metadata.episode_title) : nullptr},
            {"position", up.position},
            {"duration", up.duration},
            {"reason", ReasonName(up.reason)},
            {"image", Image(up.file_id)},
        };
        return card;
    }

    json Item(const Title& t, size_t index) const {
        const TitleItem& item = t.items[index];
        const VideoFile* best = Best(item);

        const Progress* latest = nullptr;
        json versions = json::array();
        json image = nullptr;
        for (const std::string& id : item.file_ids) {
            if (auto it = profile.progress.find(id); it != profile.progress.end()) {
                if (!latest || it->second.updated > latest->updated) latest = &it->second;
            }
            if (image.is_null()) image = Image(id);

            const VideoFile* f = catalog.FindFile(id);
            if (!f) continue;
            versions.push_back({
                {"file_id", f->id},
                {"path", f->path.string()},
                {"size", f->size},
                {"quality", Quality(f)},
                {"width", f->info ? f->info->width : 0},
                {"height", f->info ? f->info->height : 0},
                {"codec", f->info ? json(f->info->video_codec) : json(nullptr)},
                {"hdr", f->info && f->info->hdr},
                {"source", OptionalString(f->metadata.source)},
                {"tags", f->metadata.tags},
            });
        }

        json progress = nullptr;
        if (latest) {
            progress = {{"position", latest->position},
                        {"duration", latest->duration},
                        {"completed", latest->completed}};
        }
        return {
            {"file_id", item.file_ids.front()},
            {"season", item.season ? json(*item.season) : json(nullptr)},
            {"episode", item.episode ? json(*item.episode) : json(nullptr)},
            {"label", EpisodeLabel(item)},
            {"episode_title", best ? OptionalString(best->metadata.episode_title) : nullptr},
            {"duration", best && best->info ? best->info->duration : 0.0},
            {"image", image},
            {"quality", Quality(best)},
            {"progress", progress},
            {"versions", versions},
        };
    }
};

json HomeJson(const View& view, const std::string& filter, const std::vector<Profile>& everyone) {
    const Catalog& catalog = view.catalog;
    auto keep = [&](const Title& t) {
        if (filter == "show") return t.kind == TitleKind::kShow;
        if (filter == "movie") return t.kind == TitleKind::kMovie;
        return true;
    };

    std::vector<const Title*> titles;
    std::unordered_map<std::string, UpNext> up;
    for (const Title& t : catalog.titles()) {
        if (!keep(t)) continue;
        titles.push_back(&t);
        up[t.id] = ComputeUpNext(catalog, view.profile, t);
    }

    json rows = json::array();
    auto add_row = [&](const std::string& id, const std::string& name,
                       const std::vector<const Title*>& list, size_t min_items,
                       const char* type = "titles") {
        if (list.size() < min_items || list.empty()) return;
        json cards = json::array();
        for (size_t i = 0; i < list.size() && i < kRowLimit; ++i) {
            cards.push_back(view.Card(*list[i], up.at(list[i]->id)));
        }
        rows.push_back({{"id", id}, {"name", name}, {"type", type}, {"items", cards}});
    };
    auto where = [&](auto pred) {
        std::vector<const Title*> out;
        for (const Title* t : titles) {
            if (pred(*t)) out.push_back(t);
        }
        return out;
    };
    auto best_info = [&](const Title& t) -> const MediaInfo* {
        const VideoFile* f = catalog.FindFile(t.items.front().file_ids.front());
        return f && f->info ? &*f->info : nullptr;
    };

    std::vector<const Title*> continuing;
    for (const UpNext& u : ContinueWatching(catalog, view.profile)) {
        if (keep(*u.title)) continuing.push_back(u.title);
    }
    add_row("continue", "Continue Watching for " + view.profile.name, continuing, 1, "continue");

    std::vector<std::pair<std::int64_t, const Title*>> listed;
    for (const auto& [id, added] : view.profile.my_list) {
        const Title* t = catalog.FindTitle(id);
        if (t && keep(*t)) listed.emplace_back(added, t);
    }
    std::sort(listed.begin(), listed.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<const Title*> my_list;
    for (const auto& [_, t] : listed) my_list.push_back(t);
    add_row("my-list", "My List", my_list, 1);

    // Most played across every profile in the house.
    std::unordered_map<std::string, int> plays;
    for (const Profile& p : everyone) {
        for (const auto& [file_id, progress] : p.progress) {
            if (auto loc = catalog.Locate(file_id)) plays[loc->first->id] += progress.plays;
        }
    }
    std::vector<const Title*> top = where([&](const Title& t) { return plays[t.id] > 0; });
    std::stable_sort(top.begin(), top.end(),
                     [&](const Title* a, const Title* b) { return plays[a->id] > plays[b->id]; });
    if (top.size() > 10) top.resize(10);
    add_row("top10", "Top 10 in Your Library", top, 3, "top10");

    std::vector<const Title*> recent = titles;
    std::stable_sort(recent.begin(), recent.end(),
                     [](const Title* a, const Title* b) { return a->added > b->added; });
    if (recent.size() > 20) recent.resize(20);
    add_row("recent", "Recently Added", recent, 1);

    // Untouched titles in a different order each day.
    const std::int64_t day = UnixNow() / 86400;
    std::vector<const Title*> fresh = where([&](const Title& t) { return up[t.id].last_activity == 0; });
    std::shuffle(fresh.begin(), fresh.end(), std::mt19937(static_cast<unsigned>(day)));
    add_row("fresh", "New to You", fresh, 4);

    if (filter == "all") {
        add_row("shows", "TV Shows", where([](const Title& t) { return t.kind == TitleKind::kShow; }), 1);
        add_row("movies", "Movies", where([](const Title& t) { return t.kind == TitleKind::kMovie; }), 1);
    } else {
        add_row("all", filter == "show" ? "All TV Shows" : "All Movies", titles, 1);
    }

    add_row("binge", "Bingeworthy Series",
            where([](const Title& t) { return t.kind == TitleKind::kShow && t.items.size() >= 8; }), 2);
    add_row("uhd", "Stunning in 4K", where([&](const Title& t) {
                const MediaInfo* info = best_info(t);
                return info && info->height >= 1600;
            }), 2);
    add_row("epic", "Epic Movies", where([&](const Title& t) {
                const MediaInfo* info = best_info(t);
                return t.kind == TitleKind::kMovie && info && info->duration >= 2.5 * 3600;
            }), 2);
    add_row("quick", "Quick Watches", where([&](const Title& t) {
                const MediaInfo* info = best_info(t);
                return t.kind == TitleKind::kMovie && info && info->duration > 0 &&
                       info->duration < 100 * 60;
            }), 2);

    std::map<int, std::vector<const Title*>, std::greater<>> decades;
    for (const Title* t : titles) {
        if (t->year) decades[*t->year / 10 * 10].push_back(t);
    }
    for (const auto& [decade, list] : decades) {
        add_row("decade-" + std::to_string(decade), "From the " + std::to_string(decade) + "s",
                list, 3);
    }

    std::vector<const Title*> again = where([&](const Title& t) {
        return up[t.id].reason == UpNext::Reason::kRewatch && up[t.id].last_activity > 0;
    });
    std::stable_sort(again.begin(), again.end(), [&](const Title* a, const Title* b) {
        return up[a->id].last_activity > up[b->id].last_activity;
    });
    add_row("again", "Watch It Again", again, 1);

    // Billboard: something not yet watched with artwork, rotating daily
    // through the most recent additions.
    std::vector<const Title*> billboard;
    for (const Title* t : recent) {
        if (!view.TitleImage(*t).is_null() && up[t->id].last_activity == 0) billboard.push_back(t);
    }
    if (billboard.empty()) {
        for (const Title* t : recent) {
            if (!view.TitleImage(*t).is_null()) billboard.push_back(t);
        }
    }
    if (billboard.empty() && !recent.empty()) billboard.push_back(recent.front());
    if (billboard.size() > 8) billboard.resize(8);
    json hero = nullptr;
    if (!billboard.empty()) {
        const Title* t = billboard[size_t(day) % billboard.size()];
        hero = view.Card(*t, up.at(t->id));
    }

    return {{"hero", hero}, {"rows", rows}, {"count", titles.size()}};
}

std::string PlaylistTitle(const Catalog& catalog, const Title& t, size_t index) {
    const TitleItem& item = t.items[index];
    if (t.kind == TitleKind::kMovie) {
        return t.year ? t.name + " (" + std::to_string(*t.year) + ")" : t.name;
    }
    std::string title = t.name + " · " + EpisodeLabel(item);
    const VideoFile* f = catalog.FindFile(item.file_ids.front());
    if (f && f->metadata.episode_title) title += " · " + *f->metadata.episode_title;
    return title;
}

// Expands a leading "~" so settings can be typed the way people think of paths.
std::string ExpandHome(std::string path) {
    const char* home = std::getenv("HOME");
    if (home && (path == "~" || path.rfind("~/", 0) == 0)) path = home + path.substr(1);
    return path;
}

} // namespace

Server::Server(AppPaths paths, AppConfig config, LibraryService& library, WatchState& state,
               PlaybackManager& playback)
    : paths_(std::move(paths)),
      library_(library),
      state_(state),
      playback_(playback),
      config_(std::move(config)) {
    RegisterRoutes();
}

bool Server::Bind(int port) {
    port_ = port;
    return http_.bind_to_port("127.0.0.1", port);
}

void Server::Run() {
    http_.listen_after_bind();
}

void Server::Stop() {
    http_.stop();
}

void Server::WaitUntilReady() {
    http_.wait_until_ready();
}

AppConfig Server::config() const {
    std::lock_guard lock(config_mu_);
    return config_;
}

void Server::RegisterRoutes() {
    using Handled = httplib::Server::HandlerResponse;

    http_.set_pre_routing_handler([](const Request& req, Response& res) {
        // Only answer requests addressed to a loopback name. A web page that
        // re-points its own domain at 127.0.0.1 (DNS rebinding) is refused.
        std::string host = req.get_header_value("Host");
        std::string name = host.substr(0, host.rfind(':'));
        if (name != "127.0.0.1" && name != "localhost") {
            res.status = 403;
            return Handled::Handled;
        }
        // Changes need a custom header, which other sites can't send to us
        // without a CORS preflight that we never approve.
        if (req.method != "GET" && req.method != "HEAD" &&
            req.get_header_value("X-Synaxis") != "1") {
            res.status = 403;
            return Handled::Handled;
        }
        return Handled::Unhandled;
    });

    // ---- Web UI ----

    http_.Get("/", [](const Request&, Response& res) { ServeAsset("/index.html", res); });
    http_.Get(R"(/(app\.js|app\.css|icon\.svg))", [](const Request& req, Response& res) {
        if (!ServeAsset(req.path, res)) res.status = 404;
    });

    http_.Get(R"(/thumbs/([0-9a-f]{16})\.jpg)", [this](const Request& req, Response& res) {
        std::ifstream in(library_.ThumbnailPath(req.matches[1]), std::ios::binary);
        if (!in) {
            res.status = 404;
            return;
        }
        std::stringstream buf;
        buf << in.rdbuf();
        // URLs carry the thumbnail's mtime, so a cached copy is never stale.
        res.set_header("Cache-Control", "public, max-age=31536000, immutable");
        res.set_content(buf.str(), "image/jpeg");
    });

    // ---- App state ----

    http_.Get("/api/ping", [](const Request&, Response& res) {
        SendJson(res, {{"app", "synaxis"}});
    });

    http_.Get("/api/state", [this](const Request&, Response& res) {
        json profiles = json::array();
        for (const Profile& p : state_.profiles()) {
            profiles.push_back({{"id", p.id}, {"name", p.name}, {"avatar", p.avatar}});
        }
        SendJson(res, {{"profiles", profiles}, {"settings", ToJson(config())}});
    });

    http_.Get("/api/status", [this](const Request&, Response& res) {
        ScanProgress scan = library_.progress();
        json now = nullptr;
        if (auto np = playback_.now_playing()) {
            now = {{"profile", np->profile_id},
                   {"file_id", np->file_id},
                   {"title", np->title},
                   {"position", np->position},
                   {"duration", np->duration},
                   {"paused", np->paused}};
        }
        SendJson(res, {{"scan",
                        {{"running", scan.running},
                         {"phase", scan.phase},
                         {"files", scan.files},
                         {"done", scan.done},
                         {"total", scan.total},
                         {"version", scan.version},
                         {"finished_at", scan.finished_at}}},
                       {"titles", library_.catalog()->titles().size()},
                       {"now_playing", now}});
    });

    // ---- Profiles ----

    http_.Post("/api/profiles", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        std::string name = body ? StringField(*body, "name") : "";
        if (name.empty()) return SendError(res, 400, "a profile needs a name");
        Profile p = state_.AddProfile(name, body->value("avatar", 0));
        SendJson(res, {{"id", p.id}, {"name", p.name}, {"avatar", p.avatar}});
    });

    http_.Post("/api/profiles/:id", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        std::string name = body ? StringField(*body, "name") : "";
        if (name.empty()) return SendError(res, 400, "a profile needs a name");
        if (!state_.UpdateProfile(req.path_params.at("id"), name, body->value("avatar", 0))) {
            return SendError(res, 404, "no such profile");
        }
        SendJson(res, {{"ok", true}});
    });

    http_.Delete("/api/profiles/:id", [this](const Request& req, Response& res) {
        if (!state_.RemoveProfile(req.path_params.at("id"))) {
            return SendError(res, 404, "no such profile");
        }
        SendJson(res, {{"ok", true}});
    });

    // ---- Browsing ----

    // Runs `fn(view)` for the profile named in the query string.
    auto with_view = [this](const Request& req, Response& res, auto fn) {
        auto profile = state_.profile(req.get_param_value("profile"));
        if (!profile) return SendError(res, 404, "no such profile");
        std::shared_ptr<const Catalog> catalog = library_.catalog();
        fn(View{*catalog, *profile, library_});
    };

    http_.Get("/api/home", [this, with_view](const Request& req, Response& res) {
        with_view(req, res, [&](const View& view) {
            std::string filter = req.has_param("filter") ? req.get_param_value("filter") : "all";
            SendJson(res, HomeJson(view, filter, state_.profiles()));
        });
    });

    http_.Get("/api/my-list", [with_view](const Request& req, Response& res) {
        with_view(req, res, [&](const View& view) {
            std::vector<std::pair<std::int64_t, const Title*>> listed;
            for (const auto& [id, added] : view.profile.my_list) {
                if (const Title* t = view.catalog.FindTitle(id)) listed.emplace_back(added, t);
            }
            std::sort(listed.begin(), listed.end(),
                      [](auto& a, auto& b) { return a.first > b.first; });
            json cards = json::array();
            for (const auto& [_, t] : listed) cards.push_back(view.Card(*t));
            SendJson(res, {{"items", cards}});
        });
    });

    http_.Get("/api/search", [with_view](const Request& req, Response& res) {
        with_view(req, res, [&](const View& view) {
            std::string q = Simplify(req.get_param_value("q"));
            std::vector<std::pair<int, const Title*>> hits;
            for (const Title& t : view.catalog.titles()) {
                if (q.empty()) break;
                std::string name = Simplify(t.name);
                int rank = -1;
                if (name.rfind(q, 0) == 0) {
                    rank = 0;
                } else if (name.find(" " + q) != std::string::npos) {
                    rank = 1;
                } else if (name.find(q) != std::string::npos) {
                    rank = 2;
                } else {
                    for (const TitleItem& item : t.items) {
                        const VideoFile* f = view.catalog.FindFile(item.file_ids.front());
                        if (f && f->metadata.episode_title &&
                            Simplify(*f->metadata.episode_title).find(q) != std::string::npos) {
                            rank = 3;
                            break;
                        }
                    }
                }
                if (rank >= 0) hits.emplace_back(rank, &t);
            }
            std::stable_sort(hits.begin(), hits.end(),
                             [](auto& a, auto& b) { return a.first < b.first; });
            json cards = json::array();
            for (const auto& [_, t] : hits) cards.push_back(view.Card(*t));
            SendJson(res, {{"items", cards}});
        });
    });

    http_.Get("/api/titles/:id", [with_view](const Request& req, Response& res) {
        with_view(req, res, [&](const View& view) {
            const Title* t = view.catalog.FindTitle(req.path_params.at("id"));
            if (!t) return SendError(res, 404, "no such title");
            json detail = view.Card(*t);
            json items = json::array();
            for (size_t i = 0; i < t->items.size(); ++i) items.push_back(view.Item(*t, i));
            detail["items"] = items;
            const VideoFile* f = view.catalog.FindFile(t->items.front().file_ids.front());
            detail["folder"] = f ? f->path.parent_path().string() : "";
            SendJson(res, detail);
        });
    });

    // ---- Actions ----

    http_.Post("/api/play", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        if (!body) return SendError(res, 400, "bad request");
        auto profile = state_.profile(StringField(*body, "profile"));
        if (!profile) return SendError(res, 404, "no such profile");
        std::shared_ptr<const Catalog> catalog = library_.catalog();
        const Title* title = catalog->FindTitle(StringField(*body, "title"));
        if (!title) return SendError(res, 404, "no such title");

        size_t index = 0;
        std::string file_id = StringField(*body, "file");
        double start = 0;
        if (!file_id.empty()) {
            auto loc = catalog->Locate(file_id);
            if (!loc || loc->first != title) return SendError(res, 404, "no such file");
            index = loc->second;
            auto it = profile->progress.find(file_id);
            if (it != profile->progress.end() && !it->second.completed &&
                it->second.position >= kMinResumeSeconds) {
                start = it->second.position;
            }
        } else {
            UpNext up = ComputeUpNext(*catalog, *profile, *title);
            index = up.item;
            file_id = up.file_id;
            start = up.position;
        }
        if (body->value("from_start", false)) start = 0;

        const VideoFile* file = catalog->FindFile(file_id);
        std::error_code ec;
        if (!file || !fs::exists(file->path, ec)) {
            return SendError(res, 410, "That file isn't on disk anymore. Try rescanning in Settings.");
        }

        std::vector<PlaylistItem> playlist;
        playlist.push_back({file_id, file->path, PlaylistTitle(*catalog, *title, index), start});
        if (title->kind == TitleKind::kShow) {
            for (size_t i = index + 1; i < title->items.size() && i <= index + kAutoplayQueue; ++i) {
                const VideoFile* next = catalog->FindFile(title->items[i].file_ids.front());
                if (!next || !fs::exists(next->path, ec)) break;
                playlist.push_back({next->id, next->path, PlaylistTitle(*catalog, *title, i), 0});
            }
        }

        AppConfig config = this->config();
        std::string error;
        if (!playback_.Play(profile->id, std::move(playlist), {config.vlc, config.fullscreen},
                            &error)) {
            return SendError(res, 500, error);
        }
        SendJson(res, {{"ok", true}, {"start", start}});
    });

    http_.Post("/api/stop", [this](const Request&, Response& res) {
        playback_.Stop();
        SendJson(res, {{"ok", true}});
    });

    http_.Post("/api/my-list", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        if (!body) return SendError(res, 400, "bad request");
        std::string profile = StringField(*body, "profile");
        if (!state_.profile(profile)) return SendError(res, 404, "no such profile");
        bool in_list = state_.ToggleMyList(profile, StringField(*body, "title"), UnixNow());
        SendJson(res, {{"in_list", in_list}});
    });

    http_.Post("/api/watched", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        if (!body) return SendError(res, 400, "bad request");
        std::string profile = StringField(*body, "profile");
        if (!state_.profile(profile)) return SendError(res, 404, "no such profile");
        std::shared_ptr<const Catalog> catalog = library_.catalog();
        const Title* title = catalog->FindTitle(StringField(*body, "title"));
        if (!title) return SendError(res, 404, "no such title");

        // Marking applies to every copy, so it sticks whichever one plays next.
        std::vector<std::string> files;
        std::string file_id = StringField(*body, "file");
        for (const TitleItem& item : title->items) {
            bool match = file_id.empty() || std::find(item.file_ids.begin(), item.file_ids.end(),
                                                      file_id) != item.file_ids.end();
            if (match) files.insert(files.end(), item.file_ids.begin(), item.file_ids.end());
        }
        state_.SetWatched(profile, files, body->value("watched", true), UnixNow());
        SendJson(res, {{"ok", true}});
    });

    http_.Post("/api/dismiss", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        if (!body) return SendError(res, 400, "bad request");
        std::string profile = StringField(*body, "profile");
        if (!state_.profile(profile)) return SendError(res, 404, "no such profile");
        state_.Dismiss(profile, StringField(*body, "title"), UnixNow());
        SendJson(res, {{"ok", true}});
    });

    // ---- Settings ----

    http_.Post("/api/settings", [this](const Request& req, Response& res) {
        auto body = ParseBody(req);
        if (!body) return SendError(res, 400, "bad request");
        AppConfig before = config();
        AppConfig after;
        try {
            after = ApplyJson(before, *body);
        } catch (const json::exception&) {
            return SendError(res, 400, "invalid settings");
        }

        std::vector<std::string> roots;
        for (std::string root : after.roots) {
            root = ExpandHome(root);
            while (root.size() > 1 && root.back() == '/') root.pop_back();
            std::error_code ec;
            if (root.empty()) continue;
            if (!fs::is_directory(root, ec)) return SendError(res, 400, "Not a folder: " + root);
            if (std::find(roots.begin(), roots.end(), root) == roots.end()) roots.push_back(root);
        }
        after.roots = roots;
        if (after.min_size_mb < 0) after.min_size_mb = 0;
        if (after.vlc.empty()) after.vlc = "vlc";

        {
            std::lock_guard lock(config_mu_);
            config_ = after;
        }
        SaveConfig(after, paths_.config_file);

        bool rescan = after.roots != before.roots || after.min_size_mb != before.min_size_mb;
        if (rescan) library_.Rescan(after.scan_options());
        SendJson(res, {{"settings", ToJson(after)}, {"rescanning", rescan}});
    });

    http_.Post("/api/rescan", [this](const Request&, Response& res) {
        library_.Rescan(config().scan_options());
        SendJson(res, {{"ok", true}});
    });

    // Launched from the app menu there's no terminal to Ctrl+C, so the UI
    // (and `synaxis --quit`) can shut the app down. Stopping is deferred a
    // moment so this response still goes out.
    http_.Post("/api/quit", [this](const Request&, Response& res) {
        SendJson(res, {{"ok", true}});
        std::thread([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            Stop();
        }).detach();
    });
}

} // namespace synaxis
