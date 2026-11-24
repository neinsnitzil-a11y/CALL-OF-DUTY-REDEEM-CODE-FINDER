// Reads every enabled source (web pages, RSS, Google News, Google Alerts, YouTube channel feeds),
// politely: HTTPS only, robots.txt, ETag / If-Modified-Since, backs off on 403/429.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "config.hpp"
#include "html.hpp"
#include "http.hpp"
#include "intelligence.hpp"
#include "store.hpp"

namespace hunter {

struct ScanSummary {
    int sources = 0, sightings = 0, errors = 0;
    std::vector<std::string> newCodes;
    std::string startedAt, finishedAt;
};

class Discovery {
public:
    Discovery(Config& cfg, Store& store, Intelligence& intel, HttpClient& http);

    std::vector<Json> enabledSources() const;
    // stop: checked between sources. progress(name, index, total) is called before each source.
    ScanSummary scanAll(const std::atomic<bool>* stop = nullptr, bool force = false,
                        const std::function<void(const std::string&, int, int)>& progress = {});
    std::vector<Found> scanSource(const Json& src);  // one source, whatever its type

    // Optional: render a JavaScript-built page (the desktop app can plug in Qt WebEngine).
    std::function<std::string(const std::string& url)> renderer;
    std::function<void(double seconds)> sleeper;  // tests replace the polite delay

    static std::string sourceKey(const Json& src);

private:
    Json dcfg() const;
    bool due(const Json& src);
    bool blocked(const std::string& url, const std::string& name);
    void pause(const std::string& url, const std::string& name, int status, const std::string& retryAfter);
    bool robotsAllows(const std::string& url);
    SourceRow registerFor(const Json& src, const std::string& url);
    // Fetch with back-off handling; ok=false → nothing to read (logged already).
    bool fetch(const Json& src, const std::string& url, bool conditional, HttpResponse& out, SourceRow& row);
    HttpResponse get(const std::string& url, std::map<std::string, std::string> headers = {});
    void finish(const Json& src, const std::string& url, const HttpResponse& r, const std::vector<Found>& found,
                const std::string& what);
    std::vector<Found> scanWeb(const Json& src);
    std::vector<Found> scanGoogleNews(const Json& src);
    std::vector<Found> scanGoogleAlerts(const Json& src);
    std::vector<Found> scanYoutube(const Json& src);
    std::string trackRemovals(const SourceRow& row, const std::string& pageUrl, const std::vector<Found>& found);
    Document webDocument(const Json& src, const HttpResponse& r);
    std::vector<Found> processResults(const Json& src, const std::vector<FeedItem>& results, int& visited);

    Config& cfg_;
    Store& store_;
    Intelligence& intel_;
    HttpClient& http_;
    std::mutex robotsMu_;
    std::map<std::string, std::pair<Robots, long long>> robots_;
    int renders_ = 0;
};

}  // namespace hunter
