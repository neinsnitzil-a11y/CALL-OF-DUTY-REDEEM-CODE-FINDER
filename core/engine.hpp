// Everything the scanner needs, wired together. The desktop app owns one Engine; a scan cycle =
// read due sources → check every code → Discord sync → record a summary for the window.
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "checker.hpp"
#include "config.hpp"
#include "discord.hpp"
#include "discovery.hpp"
#include "intelligence.hpp"
#include "protection.hpp"
#include "store.hpp"

namespace hunter {

struct CycleResult {
    ScanSummary scan;
    SyncResult discord;
    int checked = 0;
    std::vector<std::string> pushed;  // new working codes sent to your phone
};

class Engine {
public:
    Engine(Paths paths, HttpClient& http);
    Config config;
    Store store;
    Intelligence intel;
    Discovery discovery;
    Protection protection;
    HttpClient& http;

    CycleResult runCycle(const std::atomic<bool>* stop = nullptr, bool force = false,
                         const std::function<void(const std::string&, int, int)>& progress = {});
    Json lastCycle();                  // summary of the last finished cycle (JSON in app_state)
    void emergencyStop(const std::string& why = "Emergency stop pressed");
    // Every few hours, after a scan: balance patches (meta changes, optional re-research) and new builds
    // from creators you follow. Pushes to your phone when set up. Never throws.
    Json runExtras(const std::function<void(const std::string&)>& onLine = {});
    std::function<std::string(const std::string&)> renderer;  // JavaScript pages for research / patch notes
};

}  // namespace hunter
