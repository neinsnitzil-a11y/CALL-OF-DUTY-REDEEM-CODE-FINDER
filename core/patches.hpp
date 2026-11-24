// Patch-aware meta: notice balance patches (official patch notes, your own pages, Google News about
// patch notes, and weapon stats that changed on the stats sites), see which weapons were buffed or nerfed,
// and flag builds made before the change: "outdated" (the patch named its weapon) or "check it" (a
// balance patch landed after it).
#pragma once
#include <functional>
#include <string>

#include "config.hpp"
#include "http.hpp"
#include "json.hpp"
#include "store.hpp"

namespace hunter {

class MetaData;
class SourceCatalog;
class ScreenshotLibrary;

Json weaponChanges(const std::string& text, const std::vector<std::string>& weapons);  // [{weapon, direction, detail}]
std::string parsePatchDate(const std::string& text);  // RFC 822 or ISO → ISO UTC, "" if unknown
bool mentionsBalance(const std::string& text);

Json recentPatches(Store& store, const std::string& gameId, int days = 60);
// {state: current|check|outdated, why, direction}
Json buildFreshness(Store& store, const std::string& gameId, const std::string& weapon, const std::string& asOf);

class PatchWatcher {
public:
    PatchWatcher(Config& cfg, Store& store, HttpClient& http, std::function<void(const std::string&)> onLine = {});
    Json check(const std::string& gameId);  // the NEW patches found: [{title, url, changes}]
    // Every few hours for each watched game. notify(title, message, url) for balance patches.
    Json runDue(const std::function<void(const std::string&, const std::string&, const std::string&)>& notify = {});
    std::function<std::string(const std::string&)> renderer;

private:
    bool save(const std::string& gameId, const std::string& title, const std::string& url, const std::string& published,
              const Json& changes, bool balance);
    Config& cfg_;
    Store& store_;
    HttpClient& http_;
    std::function<void(const std::string&)> onLine_;
};

}  // namespace hunter
