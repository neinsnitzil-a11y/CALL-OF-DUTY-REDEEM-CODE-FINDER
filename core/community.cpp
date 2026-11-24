#include "community.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <regex>
#include <set>

#include "content.hpp"
#include "games.hpp"
#include "patches.hpp"
#include "research.hpp"
#include "util.hpp"

namespace hunter {

namespace {
const char* kApi = "https://api.github.com";
const char* kMark = "codhunter-build";
const char* kPerf = "codhunter-perf";

std::string percentEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isAsciiAlnum(static_cast<char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string listOf(const Json& arr) {
    std::vector<std::string> v;
    for (auto& x : arr.elements()) v.push_back(x.toString());
    return join(v, ", ");
}
Json strArr(const Json& in, size_t max, size_t len) {
    Json out = Json::array();
    for (auto& x : in.elements()) if (out.size() < max) out.push(truncateUtf8(x.toString(), len));
    return out;
}
// The JSON hidden in "<!-- mark {...} -->".
std::string hiddenBlock(const std::string& body, const std::string& mark) {
    size_t p = body.find("<!--");
    while (p != std::string::npos) {
        size_t end = body.find("-->", p);
        if (end == std::string::npos) return "";
        std::string inner = trim(body.substr(p + 4, end - p - 4));
        if (startsWith(inner, mark)) {
            std::string j = trim(inner.substr(mark.size()));
            if (startsWith(j, "{")) return j;
        }
        p = body.find("<!--", end);
    }
    return "";
}
}  // namespace

double wilson(int up, int down, double z) {
    int n = up + down;
    if (!n) return 0.0;
    double p = static_cast<double>(up) / n;
    return (p + z * z / (2 * n) - z * std::sqrt((p * (1 - p) + z * z / (4 * n)) / n)) / (1 + z * z / n);
}

std::string Community::repo() const {
    std::string r = trim(jstr(cfg_.snapshot().get("community"), "repo"));
    while (!r.empty() && r.back() == '/') r.pop_back();
    if (startsWith(r, "https://github.com/")) r = r.substr(19);
    static const std::regex ok = re(R"(^[\w.-]+/[\w.-]+$)");
    return std::regex_match(r, ok) ? r : "";
}

std::pair<std::string, std::string> Community::buildIssue(const Json& b, const std::string& gameId, const std::string& note) {
    const Game& g = gameById(gameId);
    Json data = Json::object();
    data["v"] = 1;
    data["game"] = gameId;
    data["weapon"] = truncateUtf8(trim(jstr(b, "weapon")), 40);
    data["class"] = truncateUtf8(jstr(b, "class"), 30);
    data["mode"] = jstr(b, "mode", "any");
    data["playstyle"] = jstr(b, "playstyle");
    data["range"] = jstr(b, "range");
    Json att = Json::array();
    for (auto& a : b.get("attachments").elements()) {
        if (att.size() >= 12 || a.size() < 2) continue;
        Json p = Json::array();
        p.push(truncateUtf8(a.elements()[0].toString(), 30));
        p.push(truncateUtf8(a.elements()[1].toString(), 60));
        att.push(p);
    }
    data["attachments"] = att;
    data["perks"] = strArr(b.get("perks"), 6, 40);
    data["equipment"] = strArr(b.get("equipment"), 6, 40);
    data["input"] = jstr(b, "input");
    data["season"] = jstr(b, "season");
    data["date"] = todayIso();
    std::string ps = jstr(data, "playstyle"), mode = jstr(data, "mode");
    std::string title = "[" + g.label() + "] " + jstr(data, "weapon") + (!ps.empty() || mode != "any" ? " — " + (ps.empty() ? mode : ps) : "");
    std::vector<std::string> lines = {
        "**Game:** " + g.name + "  ",
        "**Weapon:** " + jstr(data, "weapon") + (jstr(data, "class").empty() ? "" : " (" + jstr(data, "class") + ")") + "  ",
        "**Mode:** " + mode + (ps.empty() ? "" : " · **Playstyle:** " + ps) + (jstr(data, "input").empty() ? "" : " · **Input:** " + jstr(data, "input")) + "  "};
    if (!jstr(data, "season").empty()) lines.push_back("**Season / patch:** " + jstr(data, "season") + "  ");
    lines.push_back("\n**Attachments**");
    for (auto& a : att.elements()) lines.push_back("- " + a.elements()[0].toString() + ": " + a.elements()[1].toString());
    if (data.get("perks").size()) lines.push_back("\n**Perks:** " + listOf(data.get("perks")));
    if (data.get("equipment").size()) lines.push_back("**Equipment:** " + listOf(data.get("equipment")));
    if (!trim(note).empty()) lines.push_back("\n" + truncateUtf8(trim(note), 1500));
    lines.push_back("\n_Vote with 👍 / 👎 on this post. Submitted with COD Redemption Hunter._");
    lines.push_back(std::string("<!-- ") + kMark + " " + data.dump() + " -->");
    return {truncateUtf8(title, 200), join(lines, "\n")};
}

std::string Community::submitUrl(const Json& build, const std::string& gameId, const std::string& note) const {
    std::string r = repo();
    if (r.empty()) throw CommunityError("Set your community repo first (Community → Set up).");
    auto [title, body] = buildIssue(build, gameId, note);
    return "https://github.com/" + r + "/issues/new?title=" + percentEncode(title) + "&body=" + percentEncode(body);
}

Json Community::call(const std::string& method, const std::string& path, const Json* payload, std::string* etag) {
    HttpRequest req;
    req.method = method;
    req.url = kApi + path;
    req.headers = {{"Accept", "application/vnd.github+json"}, {"User-Agent", "COD-Redemption-Hunter"}, {"X-GitHub-Api-Version", "2022-11-28"}};
    if (!cfg_.env("GITHUB_TOKEN").empty()) req.headers["Authorization"] = "Bearer " + cfg_.env("GITHUB_TOKEN");
    if (etag && !etag->empty()) req.headers["If-None-Match"] = *etag;
    if (payload) { req.body = payload->dump(); req.contentType = "application/json"; }
    HttpResponse r = http_.send(req);
    if (r.status == 0) throw CommunityError("Couldn't reach GitHub (" + r.error + ").");
    if (r.status == 304) return Json();  // unchanged
    if ((r.status == 401 || r.status == 403) && icontains(r.body, "rate limit"))
        throw CommunityError("GitHub's free read limit is used up for this hour — add GITHUB_TOKEN for more.");
    if (r.status == 401 || r.status == 403) throw CommunityError("GitHub refused: check GITHUB_TOKEN has Issues read & write on the community repo.");
    if (r.status == 404) throw CommunityError("GitHub can't find that repo (check the name, and that it's public or your token can see it).");
    if (r.status >= 400) throw CommunityError("GitHub answered HTTP " + std::to_string(r.status) + ": " + truncateUtf8(r.body, 160));
    if (etag) *etag = r.header("etag");
    if (trim(r.body).empty()) return Json::object();
    try { return Json::parse(r.body); } catch (...) { throw CommunityError("GitHub sent something that isn't JSON."); }
}

std::string Community::submit(const Json& build, const std::string& gameId, const std::string& note) {
    if (cfg_.env("GITHUB_TOKEN").empty()) throw CommunityError("No GITHUB_TOKEN — use the browser link instead.");
    auto [title, body] = buildIssue(build, gameId, note);
    Json p = Json::object();
    p["title"] = title;
    p["body"] = body;
    return jstr(call("POST", "/repos/" + repo() + "/issues", &p), "html_url");
}

void Community::vote(long long number, bool up) {
    if (cfg_.env("GITHUB_TOKEN").empty()) throw CommunityError("No GITHUB_TOKEN — open the build on GitHub to vote.");
    Json p = Json::object();
    p["content"] = up ? "+1" : "-1";
    call("POST", "/repos/" + repo() + "/issues/" + std::to_string(number) + "/reactions", &p);
    refresh(true);
}

void Community::shareResults(long long number, const Json& s) {
    if (cfg_.env("GITHUB_TOKEN").empty()) throw CommunityError("No GITHUB_TOKEN — sharing results needs one.");
    Json p = Json::object();
    p["body"] = "📊 My results with this build: " + std::to_string(jinteger(s, "matches")) + " matches · K/D " + s.get("kd").toString() +
                " · win rate " + std::to_string(jinteger(s, "win_rate")) + "%\n<!-- " + kPerf + " " + s.dump() + " -->";
    call("POST", "/repos/" + repo() + "/issues/" + std::to_string(number) + "/comments", &p);
}

Json Community::parseIssue(const Json& issue) {
    if (issue.contains("pull_request")) return Json();
    std::string block = hiddenBlock(jstr(issue, "body"), kMark);
    if (block.empty()) return Json();
    Json data;
    try { data = Json::parse(block); } catch (...) { return Json(); }
    if (!data.isObject() || jstr(data, "weapon").empty()) return Json();
    std::string game = jstr(data, "game");
    if (gameById(game).id != game) return Json();  // unknown game
    const Json& r = issue.get("reactions");
    int up = static_cast<int>(jinteger(r, "+1")), down = static_cast<int>(jinteger(r, "-1"));
    bool verified = false;
    for (auto& lb : issue.get("labels").elements()) if (jstr(lb, "name") == "verified") verified = true;
    Json out = data;
    out["number"] = jinteger(issue, "number");
    out["url"] = jstr(issue, "html_url");
    out["title"] = jstr(issue, "title");
    out["author"] = jstr(issue.get("user"), "login", "?");
    out["avatar"] = jstr(issue.get("user"), "avatar_url");
    out["up"] = up;
    out["down"] = down;
    out["score"] = std::round(wilson(up, down) * 10000) / 10000;
    out["comments"] = jinteger(issue, "comments");
    out["verified"] = verified;
    out["created"] = jstr(issue, "created_at");
    Json att = Json::array();
    for (auto& a : data.get("attachments").elements()) if (a.isArray() && a.size() == 2) att.push(a);
    out["attachments"] = att;
    return out;
}

Json Community::loadCache() const {
    try { return Json::parse(readFile(cfg_.paths().dataDir / "data" / "community_cache.json")); } catch (...) { return Json::object(); }
}
void Community::saveCache(const Json& c) const { writeFileAtomic(cfg_.paths().dataDir / "data" / "community_cache.json", c.dump()); }

Json Community::refresh(bool force, int maxPages) {
    std::string r = repo();
    if (r.empty()) return Json::array();
    Json cache = loadCache();
    bool same = jstr(cache, "repo") == r;
    if (same && !force && static_cast<double>(nowSeconds()) - jnum(cache, "fetched") < 600) return cache.get("builds");
    Json etags = same ? cache.get("etags") : Json::object(), pages = same ? cache.get("pages") : Json::object();
    if (!etags.isObject()) etags = Json::object();
    if (!pages.isObject()) pages = Json::object();
    Json issues = Json::array();
    for (int page = 1; page <= maxPages; ++page) {
        std::string key = std::to_string(page);
        std::string etag = force ? "" : jstr(etags, key);
        Json data = call("GET", "/repos/" + r + "/issues?state=open&per_page=100&page=" + key + "&sort=created&direction=desc", nullptr, &etag);
        if (data.isNull()) data = pages.get(key);  // 304: unchanged
        else { etags[key] = etag; pages[key] = data; }
        for (auto& i : data.elements()) issues.push(i);
        if (data.size() < 100) break;
    }
    Json builds = Json::array();
    for (auto& i : issues.elements()) { Json b = parseIssue(i); if (b.isObject()) builds.push(b); }
    cache["repo"] = r;
    cache["fetched"] = static_cast<double>(nowSeconds());
    cache["etags"] = etags;
    cache["pages"] = pages;
    cache["builds"] = builds;
    saveCache(cache);
    return builds;
}

Json Community::buildsFor(const std::string& gameId, const std::string& sort, const std::string& playstyle) const {
    Json cache = loadCache();
    if (jstr(cache, "repo") != repo()) return Json::array();
    std::vector<Json> v;
    for (auto& b : cache.get("builds").elements()) {
        if (!gameId.empty() && jstr(b, "game") != gameId) continue;
        if (!playstyle.empty() && jstr(b, "playstyle") != playstyle) continue;
        v.push_back(b);
    }
    if (sort == "new")
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return jstr(a, "created") > jstr(b, "created"); });
    else if (sort == "controversial")
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) {
            return std::min(jinteger(a, "up"), jinteger(a, "down")) > std::min(jinteger(b, "up"), jinteger(b, "down"));
        });
    else
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) {
            return std::make_tuple(!jbool(a, "verified"), -jnum(a, "score"), -jinteger(a, "up")) <
                   std::make_tuple(!jbool(b, "verified"), -jnum(b, "score"), -jinteger(b, "up"));
        });
    Json out = Json::array();
    for (auto& b : v) out.push(b);
    return out;
}

Json Community::resultsFor(long long number) {
    Json data = call("GET", "/repos/" + repo() + "/issues/" + std::to_string(number) + "/comments?per_page=100", nullptr);
    long long matches = 0, kills = 0, deaths = 0, wins = 0, players = 0;
    for (auto& c : data.elements()) {
        std::string block = hiddenBlock(jstr(c, "body"), kPerf);
        if (block.empty()) continue;
        try {
            Json s = Json::parse(block);
            ++players;
            matches += jinteger(s, "matches"); kills += jinteger(s, "kills"); deaths += jinteger(s, "deaths"); wins += jinteger(s, "wins");
        } catch (...) {}
    }
    Json out = Json::object();
    out["matches"] = matches; out["kills"] = kills; out["deaths"] = deaths; out["wins"] = wins; out["players"] = players;
    out["kd"] = deaths ? std::round(100.0 * kills / deaths) / 100 : static_cast<double>(kills);
    out["win_rate"] = matches ? static_cast<long long>(std::nearbyint(100.0 * wins / matches)) : 0LL;
    return out;
}

Json Community::creatorLinks() {
    std::string r = repo();
    if (r.empty()) return Json::object();
    Json cache = loadCache();
    if (jstr(cache, "creators_repo") == r && static_cast<double>(nowSeconds()) - jnum(cache, "creators_fetched") < 3600)
        return cache.get("creators");
    Json links = cache.get("creators").isObject() ? cache.get("creators") : Json::object();
    HttpResponse resp = http_.get("https://raw.githubusercontent.com/" + r + "/HEAD/creators.json", {{"User-Agent", "COD-Redemption-Hunter"}}, 15);
    if (resp.status == 200) {
        try { Json j = Json::parse(resp.body); links = j.isObject() ? j : Json::object(); } catch (...) {}
    } else if (resp.status) {
        links = Json::object();
    }
    cache["creators_repo"] = r;
    cache["creators_fetched"] = static_cast<double>(nowSeconds());
    cache["creators"] = links;
    saveCache(cache);
    return links;
}

Json Community::creators(const Json& builds) const {
    Json links = loadCache().get("creators");
    std::set<std::string> following;
    Json cfg = cfg_.snapshot();
    for (auto& f : cfg.get("community").get("following").elements()) following.insert(f.toString());
    struct P { std::string login, avatar, latest; int builds = 0, up = 0, down = 0, verified = 0; std::set<std::string> games; };
    std::vector<P> prof;
    for (auto& b : builds.elements()) {
        std::string a = jstr(b, "author");
        auto it = std::find_if(prof.begin(), prof.end(), [&](auto& p) { return p.login == a; });
        if (it == prof.end()) { P p; p.login = a; p.avatar = jstr(b, "avatar"); prof.push_back(p); it = std::prev(prof.end()); }
        it->builds++;
        it->up += static_cast<int>(jinteger(b, "up"));
        it->down += static_cast<int>(jinteger(b, "down"));
        it->verified += jbool(b, "verified");
        it->games.insert(jstr(b, "game"));
        it->latest = std::max(it->latest, jstr(b, "created"));
    }
    std::vector<Json> out;
    for (auto& p : prof) {
        const Json& info = links.get(p.login);
        Json o = Json::object();
        o["login"] = p.login;
        o["avatar"] = p.avatar;
        o["builds"] = p.builds;
        o["up"] = p.up;
        o["down"] = p.down;
        o["verified"] = p.verified;
        Json g = Json::array();
        for (auto& x : p.games) g.push(x);
        o["games"] = g;
        o["latest"] = p.latest;
        o["score"] = std::round(wilson(p.up, p.down) * 1000) / 1000;
        o["name"] = jstr(info, "name", p.login);
        o["bio"] = jstr(info, "bio");
        Json l = Json::object();
        for (const char* k : {"youtube", "twitch", "x", "tiktok", "kick"})
            if (startsWith(jstr(info, k), "https://")) l[k] = jstr(info, k);
        o["links"] = l;
        o["following"] = following.count(p.login) > 0;
        o["profile"] = "https://github.com/" + p.login;
        out.push_back(o);
    }
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) {
        return std::make_tuple(!jbool(a, "following"), -jnum(a, "score"), -jinteger(a, "builds")) <
               std::make_tuple(!jbool(b, "following"), -jnum(b, "score"), -jinteger(b, "builds"));
    });
    Json res = Json::array();
    for (auto& o : out) res.push(o);
    return res;
}

Json Community::newFromFollowed(Store& store, const Json& builds) const {
    std::set<std::string> following;
    Json cfg = cfg_.snapshot();
    for (auto& f : cfg.get("community").get("following").elements()) following.insert(f.toString());
    std::string raw = store.getState("community_seen");
    std::set<long long> seen;
    try { const Json arr = Json::parse(raw.empty() ? "[]" : raw); for (auto& x : arr.elements()) seen.insert(x.toInt()); } catch (...) {}
    Json fresh = Json::array();
    if (!raw.empty())  // the first look only remembers what's there
        for (auto& b : builds.elements())
            if (following.count(jstr(b, "author")) && !seen.count(jinteger(b, "number"))) fresh.push(b);
    for (auto& b : builds.elements()) seen.insert(jinteger(b, "number"));
    Json keep = Json::array();
    std::vector<long long> v(seen.begin(), seen.end());
    for (size_t i = v.size() > 2000 ? v.size() - 2000 : 0; i < v.size(); ++i) keep.push(v[i]);
    store.setState("community_seen", keep.dump());
    return fresh;
}

// ── push ────────────────────────────────────────────────────────
std::string newPushTopic() {
    static const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::string t = "codhunter-";
    for (int i = 0; i < 16; ++i) t += chars[rd() % 36];
    return t;
}
std::string pushServer(const Config& cfg) {
    std::string s = cfg.env("NTFY_SERVER", "https://ntfy.sh");
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s.empty() ? "https://ntfy.sh" : s;
}
std::string pushSubscribeUrl(const Config& cfg) {
    std::string t = cfg.env("NTFY_TOPIC");
    return t.empty() ? "" : pushServer(cfg) + "/" + t;
}
std::pair<bool, std::string> pushSend(const Config& cfg, HttpClient& http, const std::string& title, const std::string& message,
                                      const std::string& click, const std::string& tags, int priority, const std::string& event) {
    std::string topic = cfg.env("NTFY_TOPIC");
    if (topic.empty()) return {false, "no ntfy topic set"};
    Json c = cfg.snapshot();
    if (!c.get("push").get("events").get(event).toBool(true)) return {false, "push is off for this event"};
    HttpRequest req;
    req.method = "POST";
    req.url = pushServer(cfg) + "/" + topic;
    req.body = truncateUtf8(message, 3500);
    req.contentType = "text/plain; charset=utf-8";
    std::string t;  // HTTP headers: keep it Latin-1 safe
    for (unsigned char ch : title) if (ch < 0x80) t += static_cast<char>(ch);
    req.headers = {{"Title", truncateUtf8(trim(t), 200)}, {"Tags", tags}, {"Priority", std::to_string(priority)}};
    if (startsWith(click, "https://")) req.headers["Click"] = click;
    if (!cfg.env("NTFY_TOKEN").empty()) req.headers["Authorization"] = "Bearer " + cfg.env("NTFY_TOKEN");
    req.timeoutSeconds = 15;
    HttpResponse r = http.send(req);
    if (r.status == 0) return {false, "couldn't reach ntfy (" + r.error + ")"};
    return {r.status < 300, r.status < 300 ? "sent" : "ntfy answered HTTP " + std::to_string(r.status)};
}

// ── playstyle ───────────────────────────────────────────────────
const std::vector<Playstyle>& playstyles() {
    static const std::vector<Playstyle> s = {
        {"rusher", "Rusher — first in, close range", "close", {{"smg", 3}, {"shotgun", 2}, {"ar", 1}}},
        {"slayer", "Slayer — fast, mid range", "mid", {{"ar", 3}, {"smg", 2}, {"marksman", 1}}},
        {"anchor", "Anchor — hold lanes, long range", "long", {{"ar", 3}, {"lmg", 2}, {"marksman", 2}}},
        {"sniper", "Sniper — picks from distance", "long", {{"sniper", 3}, {"marksman", 2}}},
        {"support", "Support — objective & suppress", "mid", {{"lmg", 3}, {"ar", 2}}},
    };
    return s;
}

Json playerProfile(const Json& cfg) {
    Json p = Json::parse(R"({"input":"controller","playstyle":"slayer","mode":"multiplayer"})");
    for (const auto& [k, v] : cfg.get("player").items()) p[k] = v;
    return p;
}

std::string weaponClassOf(const std::string& textIn) {
    std::string t = " " + lower(textIn) + " ";
    const std::vector<std::pair<const char*, std::vector<const char*>>> table = {
        {"sniper", {"sniper"}}, {"marksman", {"marksman", "dmr", "tactical rifle"}}, {"shotgun", {"shotgun"}},
        {"lmg", {"lmg", "light machine"}}, {"smg", {"smg", "submachine"}}, {"ar", {"assault", " ar", "ar ", "rifle"}},
        {"pistol", {"pistol", "handgun"}}};
    for (auto& [key, words] : table)
        for (auto* w : words) if (t.find(w) != std::string::npos) return key;
    return "";
}

std::string weaponRangeOf(const std::string& textIn) {
    std::string t = lower(textIn);
    for (auto* w : {"close", "short", "rush", "cqb", "aggressive", "run and gun", "shredder"}) if (t.find(w) != std::string::npos) return "close";
    for (auto* w : {"long", "sniper", "lane", "range", "distance", "anchor"}) if (t.find(w) != std::string::npos) return "long";
    return "mid";
}

std::string buildKey(const std::string& gameId, const std::string& weapon) { return gameId + ":" + normKey(weapon); }

void logMatch(Store& store, const std::string& gameId, const std::string& weapon, int kills, int deaths, bool won,
              const std::string& mode, int placement, const std::string& notes) {
    store.addBuildRun(gameId, weapon, buildKey(gameId, weapon), mode, kills, deaths, won, placement, notes);
}

Json performance(Store& store, const std::string& gameId) {
    Json out = Json::object();
    Json rows = store.buildRuns(gameId);
    std::vector<Json> chrono(rows.elements().rbegin(), rows.elements().rend());  // oldest first, like Python
    for (auto& r : chrono) {
        for (const std::string& key : {jstr(r, "build_key"), std::string("*")}) {
            Json& s = out[key];
            if (!s.contains("matches")) {
                s["weapon"] = key == "*" ? "all" : jstr(r, "weapon");
                s["matches"] = 0; s["kills"] = 0; s["deaths"] = 0; s["wins"] = 0; s["last"] = "";
            }
            s["matches"] = jinteger(s, "matches") + 1;
            s["kills"] = jinteger(s, "kills") + jinteger(r, "kills");
            s["deaths"] = jinteger(s, "deaths") + jinteger(r, "deaths");
            s["wins"] = jinteger(s, "wins") + jinteger(r, "won");
            s["last"] = std::max(jstr(s, "last"), jstr(r, "played_at"));
        }
    }
    for (auto& [k, s] : out.items()) {
        long long kills = jinteger(s, "kills"), deaths = jinteger(s, "deaths"), m = jinteger(s, "matches");
        s["kd"] = deaths ? std::round(100.0 * kills / deaths) / 100 : static_cast<double>(kills);
        s["win_rate"] = m ? static_cast<long long>(std::nearbyint(100.0 * jinteger(s, "wins") / m)) : 0LL;
    }
    return out;
}

Json buildCandidates(const Config& cfg, const std::string& gameId, const Json& communityBuilds) {
    Json out = Json::array();
    MetaData meta(cfg.paths());
    Json m = meta.forGame(gameId);
    for (auto& b : m.get("builds").elements()) {
        Json c = Json::object();
        c["weapon"] = jstr(b, "weapon"); c["class"] = jstr(b, "class"); c["role"] = jstr(b, "role"); c["tier"] = jstr(b, "tier");
        c["attachments"] = b.get("attachments").isArray() ? b.get("attachments") : Json::array();
        c["origin"] = "curated";
        c["as_of"] = jstr(b, "as_of", jstr(m, "updated"));
        out.push(c);
    }
    Json rs = ResearchStore(cfg.paths()).forGame(gameId).get("builds");
    for (auto& r : rs.get("weapons").elements()) {
        Json c = Json::object();
        c["weapon"] = jstr(r, "weapon"); c["class"] = jstr(r, "class"); c["role"] = jstr(r, "role"); c["tier"] = "";
        Json att = Json::array();
        for (auto& a : r.get("attachments").elements()) {
            Json p = Json::array();
            p.push(a.elements()[0]);
            p.push(a.elements()[1]);
            att.push(p);
        }
        c["attachments"] = att;
        c["origin"] = "researched";
        c["verified"] = jbool(r, "verified");
        c["sites"] = jinteger(r, "sites");
        c["as_of"] = jstr(rs, "updated").substr(0, 10);
        out.push(c);
    }
    const Json mine = MyBuilds(cfg.paths()).forGame(gameId);
    for (auto& b : mine.elements()) {
        Json c = Json::object();
        c["weapon"] = jstr(b, "weapon"); c["class"] = jstr(b, "class");
        c["role"] = trim(jstr(b, "role") + " " + jstr(b, "range") + " " + jstr(b, "mode"));
        c["tier"] = "";
        c["attachments"] = b.get("attachments");
        c["origin"] = "yours";
        c["playstyle"] = jstr(b, "playstyle");
        c["id"] = jstr(b, "id");
        c["as_of"] = jstr(b, "updated").substr(0, 10);
        out.push(c);
    }
    for (auto& b : communityBuilds.elements()) {
        if (jstr(b, "game") != gameId) continue;
        Json c = Json::object();
        c["weapon"] = jstr(b, "weapon"); c["class"] = jstr(b, "class");
        c["role"] = jstr(b, "playstyle") + " " + jstr(b, "range") + " " + jstr(b, "mode");
        c["tier"] = "";
        c["attachments"] = b.get("attachments");
        c["origin"] = "community";
        c["up"] = jinteger(b, "up"); c["down"] = jinteger(b, "down"); c["score"] = jnum(b, "score");
        c["community_verified"] = jbool(b, "verified");
        c["author"] = jstr(b, "author");
        c["playstyle"] = jstr(b, "playstyle");
        c["url"] = jstr(b, "url");
        c["number"] = jinteger(b, "number");
        c["as_of"] = jstr(b, "date").empty() ? jstr(b, "created").substr(0, 10) : jstr(b, "date");
        out.push(c);
    }
    return out;
}

Json recommend(Config& cfg, Store& store, const Json& candidates, const std::string& gameId, size_t limit, const Json& profIn) {
    Json prof = profIn.isObject() ? profIn : playerProfile(cfg.snapshot());
    const Playstyle* style = &playstyles()[1];
    for (auto& p : playstyles()) if (p.id == jstr(prof, "playstyle")) style = &p;
    Json perf = performance(store, gameId);
    double avg = perf.get("*").get("kd").toDouble(0);
    std::vector<Json> ranked;
    for (const auto& cIn : candidates.elements()) {
        Json c = cIn;
        std::vector<std::string> reasons;
        double score = 0;
        std::string cls = weaponClassOf(jstr(c, "class") + " " + jstr(c, "weapon"));
        if (cls.empty()) cls = weaponClassOf(jstr(c, "role"));
        std::string rng = weaponRangeOf(jstr(c, "role"));
        int fit = 0;
        for (auto& [k, v] : style->classes) if (k == cls) fit = v;
        if (fit) {
            score += fit;
            reasons.push_back((cls == "smg" || cls == "ar" || cls == "lmg" ? upper(cls) : cls) + " suits a " + jstr(prof, "playstyle"));
        }
        if (rng == style->range) { score += 1.5; reasons.push_back(rng + "-range build"); }
        if (jstr(c, "playstyle") == jstr(prof, "playstyle") && !jstr(c, "playstyle").empty()) { score += 1.5; reasons.push_back("made for your playstyle"); }
        if (lower(jstr(c, "role")).find(jstr(prof, "mode")) != std::string::npos && !jstr(prof, "mode").empty()) {
            score += 1;
            reasons.push_back("built for " + jstr(prof, "mode"));
        }
        std::string tier = jstr(c, "tier");
        if (tier == "S" || tier == "A") { score += tier == "S" ? 2 : 1; reasons.push_back(tier + " tier"); }
        if (jstr(c, "origin") == "yours") { score += 1; reasons.push_back("your own build"); }
        if (jbool(c, "verified")) { score += 1; reasons.push_back(std::to_string(jinteger(c, "sites")) + " sites agree"); }
        if (jstr(c, "origin") == "community" && jinteger(c, "up") + jinteger(c, "down")) {
            score += std::min(3.0, 5 * jnum(c, "score"));
            reasons.push_back("👍 " + std::to_string(jinteger(c, "up")) + " 👎 " + std::to_string(jinteger(c, "down")) + " in the community");
            if (jbool(c, "community_verified")) { score += 1; reasons.push_back("verified by the community moderators"); }
        }
        Json fr = buildFreshness(store, gameId, jstr(c, "weapon"), jstr(c, "as_of"));
        if (jstr(fr, "state") == "outdated") {
            score += jstr(fr, "direction") == "buff" ? 1 : -2.5;
            reasons.push_back("⚠ " + jstr(fr, "why"));
        } else if (jstr(fr, "state") == "check") {
            score -= 0.5;
            reasons.push_back("check: " + jstr(fr, "why"));
        }
        const Json& p = perf.get(buildKey(gameId, jstr(c, "weapon")));
        if (p.isObject() && jinteger(p, "matches") >= 3 && avg) {
            double kd = jnum(p, "kd");
            double delta = std::max(-2.0, std::min(2.0, (kd - avg) / std::max(avg, 0.1) * 4));
            score += delta;
            char buf[160];
            std::snprintf(buf, sizeof buf, "your K/D with it %.2f vs your average %.2f (%lld matches)", kd, avg, jinteger(p, "matches"));
            reasons.push_back(buf);
        }
        if (jstr(prof, "input") == "touch" && gameId != "codm") score -= 0.5;
        c["fit"] = std::round(score * 100) / 100;
        Json rs = Json::array();
        for (auto& r : reasons) rs.push(r);
        c["reasons"] = rs;
        c["freshness"] = fr;
        c["weapon_class"] = cls;
        c["range"] = rng;
        ranked.push_back(c);
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](auto& a, auto& b) { return jnum(a, "fit") > jnum(b, "fit"); });
    Json out = Json::array();
    std::set<std::string> seen;
    for (auto& r : ranked) {  // one build per weapon: the best-scoring version
        if (out.size() >= limit) break;
        if (seen.insert(normKey(jstr(r, "weapon"))).second) out.push(r);
    }
    return out;
}

}  // namespace hunter
