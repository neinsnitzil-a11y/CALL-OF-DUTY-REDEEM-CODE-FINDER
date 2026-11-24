#include "discovery.hpp"

#include <algorithm>
#include <chrono>
#include <regex>
#include <set>
#include <thread>

#include "util.hpp"
#include "validation.hpp"

namespace hunter {

namespace {
const std::set<std::string> kFeedReaders = {"google_news", "google_alerts", "youtube"};

int retryAfterMinutes(const std::string& v, int fallback) {
    if (v.empty()) return fallback;
    try { return std::max(1, std::stoi(v) / 60); } catch (...) { return fallback; }
}
std::vector<std::string> codesOf(const std::vector<Found>& f) {
    std::vector<std::string> out;
    for (auto& x : f) out.push_back(x.code);
    return out;
}
size_t distinctCodes(const std::vector<Found>& f) {
    std::set<std::string> s;
    for (auto& x : f) s.insert(x.code);
    return s.size();
}
}  // namespace

Discovery::Discovery(Config& cfg, Store& store, Intelligence& intel, HttpClient& http)
    : cfg_(cfg), store_(store), intel_(intel), http_(http) {
    sleeper = [](double s) { std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(s * 1000))); };
}

Json Discovery::dcfg() const { return cfg_.snapshot().get("discovery"); }

std::string Discovery::sourceKey(const Json& src) {
    std::string url = jstr(src, "url");
    if (!url.empty()) return url;
    std::string q = jstr(src, "query");
    return jstr(src, "type") + ":" + (q.empty() ? jstr(src, "channel") : q);
}

std::vector<Json> Discovery::enabledSources() const {
    std::vector<Json> out;
    const Json cfg = cfg_.snapshot();
    for (const auto& s : cfg.get("sources").elements())
        if (jbool(s, "enabled") && (!jstr(s, "url").empty() || kFeedReaders.count(jstr(s, "type"))))
            out.push_back(s);
    return out;
}

bool Discovery::due(const Json& src) {
    double minutes = jnum(src, "interval_minutes", 0);
    if (minutes <= 0) return true;
    std::string last = store_.getState("last_scan:" + sourceKey(src));
    auto t = parseIso(last);
    return !t || static_cast<double>(*t) <= static_cast<double>(nowSeconds()) - minutes * 60;
}

HttpResponse Discovery::get(const std::string& url, std::map<std::string, std::string> headers) {
    Json d = dcfg();
    headers["User-Agent"] = jstr(d, "user_agent", "CODRedemptionHunter/6.0 (open-source personal code monitor)");
    return http_.get(url, headers, static_cast<int>(jinteger(d, "request_timeout_seconds", 20)));
}

bool Discovery::blocked(const std::string& url, const std::string& name) {
    auto row = store_.getSource(url);
    if (row && startsWith(row->lastStatus, "BLOCKED")) {
        std::string until = store_.getState("blocked_until:" + url);
        if (!until.empty() && until > utcNowIso()) {
            store_.logScan(name, "SKIPPED", "paused until " + until + " (site asked us to back off)");
            return true;
        }
    }
    return false;
}

void Discovery::pause(const std::string& url, const std::string& name, int status, const std::string& retryAfter) {
    int minutes = retryAfterMinutes(retryAfter, 360);
    std::string until = utcIso(static_cast<std::time_t>(nowSeconds() + minutes * 60LL));
    store_.setState("blocked_until:" + url, until);
    store_.updateSourceHealth(url, false, "BLOCKED HTTP " + std::to_string(status));
    store_.logScan(name, "BLOCKED", "HTTP " + std::to_string(status) + "; paused until " + until);
    store_.addAudit("SOURCE_BLOCKED", name + " HTTP " + std::to_string(status));
}

bool Discovery::robotsAllows(const std::string& url) {
    Url u = parseUrl(url);
    std::string base = u.scheme + "://" + u.host;
    {
        std::lock_guard lock(robotsMu_);
        auto it = robots_.find(base);
        if (it != robots_.end() && it->second.second > nowSeconds() - 86400) return it->second.first.allows(url);
    }
    HttpResponse r = get(base + "/robots.txt");
    Robots rb(r.status == 200 ? r.body : "", jstr(dcfg(), "user_agent", "CODRedemptionHunter"));
    std::lock_guard lock(robotsMu_);
    robots_[base] = {rb, nowSeconds()};
    return rb.allows(url);
}

SourceRow Discovery::registerFor(const Json& src, const std::string& url) {
    Json cfg = cfg_.snapshot();
    int score = baseScore(jstr(src, "trust", "unverified"), cfg);
    if (auto row = store_.getSource(url)) score = healthAdjusted(score, row->okCount, row->errorCount);
    return store_.registerSource(jstr(src, "name", url), url, jstr(src, "type", "web"), jstr(src, "trust", "unverified"), score);
}

bool Discovery::fetch(const Json& src, const std::string& url, bool conditional, HttpResponse& out, SourceRow& row) {
    std::string name = jstr(src, "name", url);
    if (!startsWith(url, "https://")) throw std::runtime_error("Only HTTPS sources are allowed.");
    if (blocked(url, name)) return false;
    bool feedReader = kFeedReaders.count(jstr(src, "type")) || jbool(src, "feed_reader");
    if (!feedReader && jbool(dcfg(), "respect_robots_txt", true) && !robotsAllows(url)) {
        store_.logScan(name, "SKIPPED", "the site's robots.txt asks automated tools not to read this page");
        store_.updateSourceHealth(url, false, "ROBOTS_DISALLOWED");
        return false;
    }
    row = registerFor(src, url);
    std::map<std::string, std::string> headers;
    if (conditional && !row.etag.empty()) headers["If-None-Match"] = row.etag;
    if (conditional && !row.lastModified.empty()) headers["If-Modified-Since"] = row.lastModified;
    out = get(url, headers);
    if (out.status == 403 || out.status == 429) {
        pause(url, name, out.status, out.header("retry-after"));
        return false;
    }
    if (out.status == 304) {
        store_.updateSourceHealth(url, true, "NOT_MODIFIED");
        store_.logScan(name, "OK", "not modified");
        return false;
    }
    if (out.status == 0) throw std::runtime_error(out.error.empty() ? "network error" : out.error);
    if (out.status >= 400) throw std::runtime_error("HTTP " + std::to_string(out.status));
    return true;
}

void Discovery::finish(const Json& src, const std::string& url, const HttpResponse& r, const std::vector<Found>& found,
                       const std::string& what) {
    int fresh = static_cast<int>(std::count_if(found.begin(), found.end(), [](const Found& f) { return f.isNew; }));
    store_.updateSourceHealth(url, true, "OK", r.header("etag"), r.header("last-modified"), fresh);
    store_.logScan(jstr(src, "name", url), "OK", what + ", " + std::to_string(distinctCodes(found)) + " code(s) seen");
}

ScanSummary Discovery::scanAll(const std::atomic<bool>* stop, bool force,
                               const std::function<void(const std::string&, int, int)>& progress) {
    ScanSummary sum;
    sum.startedAt = utcNowIso();
    renders_ = 0;
    std::vector<Json> sources;
    for (auto& s : enabledSources()) if (force || due(s)) sources.push_back(s);
    Json d = dcfg();
    size_t cap = static_cast<size_t>(std::max<long long>(1, jinteger(d, "max_sources_per_cycle", 40)));
    if (sources.size() > cap) sources.resize(cap);
    double delay = jnum(d, "delay_between_sources_seconds", 2.0);
    for (size_t i = 0; i < sources.size(); ++i) {
        if (stop && stop->load()) break;
        const Json& src = sources[i];
        std::string name = jstr(src, "name", "?");
        if (progress) progress(name, static_cast<int>(i), static_cast<int>(sources.size()));
        try {
            auto found = scanSource(src);
            ++sum.sources;
            sum.sightings += static_cast<int>(found.size());
            for (auto& f : found)
                if (f.isNew && std::find(sum.newCodes.begin(), sum.newCodes.end(), f.code) == sum.newCodes.end())
                    sum.newCodes.push_back(f.code);
        } catch (const std::exception& e) {  // one bad source never stops the cycle
            ++sum.errors;
            store_.logScan(name, "ERROR", e.what());
            if (!jstr(src, "url").empty()) store_.updateSourceHealth(jstr(src, "url"), false, truncateUtf8(std::string("ERROR: ") + e.what(), 200));
        }
        store_.setState("last_scan:" + sourceKey(src), utcNowIso());
        if (i + 1 < sources.size() && sleeper) sleeper(delay);
    }
    intel_.reassessAll();
    sum.finishedAt = utcNowIso();
    return sum;
}

std::vector<Found> Discovery::scanSource(const Json& src) {
    std::string type = jstr(src, "type", "web");
    if (type == "web" || type == "rss") return scanWeb(src);
    if (type == "google_news") return scanGoogleNews(src);
    if (type == "google_alerts") return scanGoogleAlerts(src);
    if (type == "youtube") return scanYoutube(src);
    store_.logScan(jstr(src, "name", "?"), "SKIPPED", "unknown source type '" + type + "'");
    return {};
}

Document Discovery::webDocument(const Json& src, const HttpResponse& r) {
    std::string url = r.finalUrl.empty() ? jstr(src, "url") : r.finalUrl;
    Document doc = htmlToDocument(r.body, url);
    std::string visible;
    for (auto& b : doc.blocks) visible += b.text + " ";
    bool jsPage = looksJsRendered(r.body, visible);
    bool want = jbool(src, "render") || (jsPage && jbool(dcfg(), "auto_render_js", true));
    if (!want) return doc;
    std::string name = jstr(src, "name", url);
    if (renders_ >= jinteger(dcfg(), "max_renders_per_cycle", 3)) {
        store_.logScan(name, "OK", "page is drawn by JavaScript — render budget for this scan used up");
        return doc;
    }
    if (!renderer) {
        if (jsPage) store_.logScan(name, "OK", "page looks JavaScript-rendered; only its plain HTML was read");
        return doc;
    }
    try {
        ++renders_;
        Document rendered = htmlToDocument(renderer(url), url);
        return rendered.blocks.size() >= doc.blocks.size() ? rendered : doc;
    } catch (const std::exception& e) {
        store_.logScan(name, "OK", std::string("JavaScript render failed (") + truncateUtf8(e.what(), 80) + "); used the plain page");
        return doc;
    }
}

std::string Discovery::trackRemovals(const SourceRow& row, const std::string& pageUrl, const std::vector<Found>& found) {
    int before = store_.activeSightingsOn(row.id, pageUrl);
    auto present = codesOf(found);
    std::set<std::string> distinct(present.begin(), present.end());
    if (before && static_cast<double>(distinct.size()) < std::max(1.0, before * 0.3))
        return ", removal check skipped (page shows " + std::to_string(distinct.size()) + " codes, had " + std::to_string(before) + ")";
    auto gone = store_.markRemoved(row.id, pageUrl, present);
    for (auto id : gone) intel_.reassess(id);
    return gone.empty() ? "" : ", " + std::to_string(gone.size()) + " code(s) taken down";
}

std::vector<Found> Discovery::scanWeb(const Json& src) {
    std::string url = jstr(src, "url");
    HttpResponse r;
    SourceRow row;
    if (!fetch(src, url, true, r, row)) return {};
    std::string type = jstr(src, "type", "web");
    std::vector<Document> docs = type == "rss" ? parseFeed(r.body, r.finalUrl.empty() ? url : r.finalUrl)
                                               : std::vector<Document>{webDocument(src, r)};
    std::vector<Found> found;
    for (auto& d : docs) {
        auto f = intel_.processDocument(d, row, jstr(src, "game"));
        found.insert(found.end(), f.begin(), f.end());
    }
    std::string what = std::to_string(docs.size()) + " document(s)";
    if (type == "web") what += trackRemovals(row, docs.front().url, found);
    store_.setContentTime(url);
    finish(src, url, r, found, what);
    return found;
}

std::vector<Found> Discovery::scanGoogleNews(const Json& src) {
    std::string url = googleNewsUrl(jstr(src, "query", "COD Mobile redeem codes"), static_cast<int>(jinteger(src, "days", 2)),
                                    jstr(src, "lang", "en"), jstr(src, "country", "US"));
    HttpResponse r;
    SourceRow row;
    if (!fetch(src, url, false, r, row)) return {};
    auto items = parseFeedItems(r.body);
    std::vector<Found> found;
    for (auto& it : items) {
        auto f = intel_.processDocument(htmlToDocument("<h1>" + escapeHtml(it.title) + "</h1>" + it.summary, it.link), row,
                                        jstr(src, "game"));
        found.insert(found.end(), f.begin(), f.end());
    }
    finish(src, url, r, found, std::to_string(items.size()) + " headline(s)");
    return found;
}

std::vector<Found> Discovery::scanGoogleAlerts(const Json& src) {
    std::string url = trim(jstr(src, "url"));
    if (url.empty()) {
        store_.logScan(jstr(src, "name", "Google Alerts"), "SKIPPED",
                       "no feed yet — create a Google Alert with 'Deliver to: RSS feed' and paste it in Setup");
        return {};
    }
    HttpResponse r;
    SourceRow row;
    if (!fetch(src, url, false, r, row)) return {};
    auto items = parseFeedItems(r.body);
    for (auto& it : items) it.link = realResultUrl(it.link);
    int visited = 0;
    auto found = processResults(src, items, visited);
    finish(src, url, r, found, std::to_string(items.size()) + " result(s), " + std::to_string(visited) + " page(s) read");
    return found;
}

std::vector<Found> Discovery::processResults(const Json& src, const std::vector<FeedItem>& results, int& visited) {
    Json cfg = cfg_.snapshot();
    std::map<std::string, std::string> trustByDomain;
    for (const auto& s : cfg.get("sources").elements()) {
        std::string t = jstr(s, "type");
        if (!jstr(s, "url").empty() && (t == "web" || t == "rss")) trustByDomain[urlDomain(jstr(s, "url"))] = jstr(s, "trust", "unverified");
    }
    std::vector<Found> found;
    long long maxPages = jinteger(src, "max_pages", 6);
    double refetchHours = jnum(src, "refetch_hours", 6);
    for (const auto& res : results) {
        const std::string& url = res.link;
        if (!startsWith(url, "https://")) continue;
        std::string dom = urlDomain(url);
        auto t = trustByDomain.find(dom);
        std::string trust = t != trustByDomain.end() ? t->second : jstr(src, "trust", "unverified");
        SourceRow row = store_.registerSource("Search result · " + dom, "https://" + dom + "/#search", "search-result", trust,
                                              baseScore(trust, cfg));
        Document snippetDoc{url, res.title, {{res.title, collapseSpaces(htmlToText(res.summary))}}};
        auto f = intel_.processDocument(snippetDoc, row, jstr(src, "game"));
        found.insert(found.end(), f.begin(), f.end());
        if (visited >= maxPages || t != trustByDomain.end()) continue;  // monitored sites are read by their own source
        auto last = parseIso(store_.getState("result_fetched:" + url));
        if (last && static_cast<double>(*last) > static_cast<double>(nowSeconds()) - refetchHours * 3600) continue;
        if (jbool(dcfg(), "respect_robots_txt", true) && !robotsAllows(url)) continue;
        store_.setState("result_fetched:" + url, utcNowIso());
        ++visited;
        if (sleeper) sleeper(jnum(dcfg(), "delay_between_sources_seconds", 2.0));
        HttpResponse page = get(url);
        std::string ctype = page.header("content-type");
        if (page.status == 200 && (ctype.empty() || icontains(ctype, "html"))) {
            auto g = intel_.processDocument(htmlToDocument(page.body, page.finalUrl.empty() ? url : page.finalUrl), row,
                                            jstr(src, "game"));
            found.insert(found.end(), g.begin(), g.end());
        }
    }
    return found;
}

std::vector<Found> Discovery::scanYoutube(const Json& src) {
    std::string channel = jstr(src, "channel_id");
    if (channel.empty()) {
        std::string handle = trim(jstr(src, "channel"));
        if (!handle.empty() && handle[0] == '@') handle = handle.substr(1);
        if (handle.empty()) {
            store_.logScan(jstr(src, "name", "YouTube"), "SKIPPED", "no channel handle or channel_id set");
            return {};
        }
        channel = store_.getState("yt_channel:" + lower(handle));
        if (channel.empty()) {
            std::string pageUrl = "https://www.youtube.com/@" + handle;
            if (jbool(dcfg(), "respect_robots_txt", true) && !robotsAllows(pageUrl))
                throw std::runtime_error("YouTube's robots.txt disallows the channel page; set channel_id instead");
            HttpResponse page = get(pageUrl, {{"Accept-Language", "en"}});
            static const std::regex rx = re(R"rx("(?:channelId|externalId)"\s*:\s*"(UC[\w-]{22})"|/channel/(UC[\w-]{22}))rx");
            std::smatch m;
            if (page.status != 200 || !std::regex_search(page.body, m, rx))
                throw std::runtime_error("couldn't find a channel ID for @" + handle + " (HTTP " + std::to_string(page.status) + ")");
            channel = m[1].matched ? m[1].str() : m[2].str();
            store_.setState("yt_channel:" + lower(handle), channel);
        }
    }
    std::string url = "https://www.youtube.com/feeds/videos.xml?channel_id=" + channel;
    HttpResponse r;
    SourceRow row;
    if (!fetch(src, url, false, r, row)) return {};
    auto items = parseFeedItems(r.body);
    std::vector<Found> found;
    for (auto& it : items) {
        auto f = intel_.processDocument(
            htmlToDocument("<h1>" + escapeHtml(it.title) + "</h1><p>" + escapeHtml(it.summary) + "</p>", it.link), row,
            jstr(src, "game"));
        found.insert(found.end(), f.begin(), f.end());
    }
    finish(src, url, r, found, std::to_string(items.size()) + " video(s)");
    return found;
}

}  // namespace hunter
