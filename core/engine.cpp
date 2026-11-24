#include "engine.hpp"

#include "community.hpp"
#include "games.hpp"
#include "patches.hpp"
#include "util.hpp"

namespace hunter {

Engine::Engine(Paths paths, HttpClient& h)
    : config(std::move(paths)),
      store(config.paths().database().string()),
      intel(config, store),
      discovery(config, store, intel, h),
      protection(config, store),
      http(h) {}

CycleResult Engine::runCycle(const std::atomic<bool>* stop, bool force,
                             const std::function<void(const std::string&, int, int)>& progress) {
    CycleResult r;
    store.setState("scanner_state", "scanning");
    r.scan = discovery.scanAll(stop, force, progress);
    r.checked = checkAll(store);
    if (!(stop && stop->load())) {
        DiscordNotifier discord(config, store, http);
        r.discord = discord.sync();
        // phone push for new codes that are ready
        std::vector<std::string> lines;
        for (auto& c : r.scan.newCodes) {
            auto row = store.getCodeByText(c);
            if (!row || row->status != "READY") continue;
            r.pushed.push_back(c);
            lines.push_back(gameById(row->game).label() + " · " + row->code + " · " +
                            (row->game == "codm" ? row->region : (row->platform.empty() ? "All platforms" : row->platform)));
        }
        if (!lines.empty()) {
            if (lines.size() > 6) lines.resize(6);
            pushSend(config, http, std::to_string(r.pushed.size()) + " new Call of Duty code" + (r.pushed.size() > 1 ? "s" : "") + " ready",
                     join(lines, "\n"), "", "dart,video_game", 4, "codes");
        }
    }
    Json summary = Json::object();
    summary["started_at"] = r.scan.startedAt;
    summary["finished_at"] = r.scan.finishedAt;
    summary["sources"] = r.scan.sources;
    summary["sightings"] = r.scan.sightings;
    summary["errors"] = r.scan.errors;
    Json codes = Json::array();
    for (auto& c : r.scan.newCodes) codes.push(c);
    summary["new_codes"] = codes;
    summary["discord_new"] = r.discord.posted;
    summary["discord_error"] = r.discord.error;
    store.setState("last_cycle", summary.dump());
    store.setState("scanner_state", "idle");
    return r;
}

Json Engine::lastCycle() {
    try { return Json::parse(store.getState("last_cycle", "{}")); } catch (...) { return Json::object(); }
}

Json Engine::runExtras(const std::function<void(const std::string&)>& onLine) {
    Json out = Json::object();
    try {
        PatchWatcher w(config, store, http, onLine);
        w.renderer = renderer;
        out["patches"] = w.runDue([&](const std::string& title, const std::string& msg, const std::string& url) {
            pushSend(config, http, title, msg, url, "warning,video_game", 4, "patch");
        });
    } catch (const std::exception& e) {
        store.logScan("Patch watch", "ERROR", e.what());
    }
    try {
        Community c(config, http);
        if (!c.repo().empty()) {
            auto last = Json(store.getState("community_checked", "0")).toDouble(0);
            if (static_cast<double>(nowSeconds()) - last > 1800) {
                store.setState("community_checked", std::to_string(nowSeconds()));
                Json builds = c.refresh();
                Json fresh = c.newFromFollowed(store, builds);
                for (size_t i = 0; i < fresh.size() && i < 5; ++i) {
                    const Json& b = fresh.elements()[i];
                    pushSend(config, http, "New build from " + jstr(b, "author"), jstr(b, "title"), jstr(b, "url"), "gun", 4, "community");
                }
                out["community_builds"] = static_cast<long long>(builds.size());
            }
        }
    } catch (const std::exception& e) {
        store.logScan("Community", "ERROR", e.what());
    }
    return out;
}

void Engine::emergencyStop(const std::string& why) {
    protection.breaker.trip(why);
    store.addAudit("EMERGENCY_STOP", why);
    DiscordNotifier(config, store, http).systemAlert("EMERGENCY_STOP", why);
}

}  // namespace hunter
