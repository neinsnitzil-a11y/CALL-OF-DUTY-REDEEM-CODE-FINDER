#include "patches.hpp"

#include <algorithm>
#include <cstdio>
#include <regex>
#include <set>

#include "ai.hpp"
#include "content.hpp"
#include "games.hpp"
#include "html.hpp"
#include "research.hpp"
#include "stats.hpp"
#include "util.hpp"

namespace hunter {

namespace {
const std::vector<std::string> kCurrent = {"mw4", "warzone", "bo7", "bo6", "codm"};
const char* kOfficialPatchNotes = "https://www.callofduty.com/patchnotes";

const std::regex& buffRe() {
    static const std::regex r = re(
        R"(increas\w* (?:damage|range|fire rate|velocity|mag|ammo|headshot|mobility|ads speed)|)"
        R"((?:reduc|decreas)\w* (?:recoil|ads time|sprint[- ]to[- ]fire|idle sway|flinch|bounce|kick)|)"
        R"((?:damage|range|fire rate|velocity|mobility|ads speed)\W+(?:\w+\W+){0,2}increas|)"
        R"((?:recoil|ads time|sprint[- ]to[- ]fire|idle sway|flinch)\W+(?:\w+\W+){0,2}(?:reduc|decreas|lower)|)"
        R"(\bbuff\w*|\bimproved?\b|\bfaster\b)",
        true);
    return r;
}
const std::regex& nerfRe() {
    static const std::regex r = re(
        R"((?:reduc|decreas|lower)\w* (?:damage|range|fire rate|velocity|mag|ammo|headshot|mobility)|)"
        R"(increas\w* (?:recoil|ads time|sprint[- ]to[- ]fire|idle sway|flinch|bounce|kick)|)"
        R"((?:damage|range|fire rate|velocity|mobility)\W+(?:\w+\W+){0,2}(?:reduc|decreas|lower)|)"
        R"((?:recoil|ads time|sprint[- ]to[- ]fire|idle sway|flinch)\W+(?:\w+\W+){0,2}increas|)"
        R"(\bnerf\w*|\bslower\b)",
        true);
    return r;
}
const std::regex& balanceRe() {
    static const std::regex r = re(R"(weapon(?:s)? (?:balanc|tuning|adjust|changes)|balance (?:update|changes|patch)|buffs?|nerfs?)", true);
    return r;
}

// Python: re.split(r"[\n\r]+|(?<=[.!?])\s+(?=[A-Z•\-])", text)
std::vector<std::string> splitSentences(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n' || c == '\r') {
            out.push_back(cur);
            cur.clear();
            while (i + 1 < text.size() && (text[i + 1] == '\n' || text[i + 1] == '\r')) ++i;
            continue;
        }
        if ((c == ' ' || c == '\t') && !cur.empty() && (cur.back() == '.' || cur.back() == '!' || cur.back() == '?')) {
            size_t j = i;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) ++j;
            bool next = j < text.size() && ((text[j] >= 'A' && text[j] <= 'Z') || text[j] == '-' ||
                                            text.compare(j, 3, "\xE2\x80\xA2") == 0);
            if (next) {
                out.push_back(cur);
                cur.clear();
                i = j - 1;
                continue;
            }
        }
        cur += c;
    }
    out.push_back(cur);
    return out;
}

std::string stripBullets(std::string s) {
    while (true) {
        size_t before = s.size();
        while (!s.empty() && std::string(" -*\t").find(s.front()) != std::string::npos) s.erase(0, 1);
        while (!s.empty() && std::string(" -*\t").find(s.back()) != std::string::npos) s.pop_back();
        if (startsWith(s, "\xE2\x80\xA2")) s.erase(0, 3);
        if (endsWith(s, "\xE2\x80\xA2")) s.resize(s.size() - 3);
        if (s.size() == before) return s;
    }
}
}  // namespace

bool mentionsBalance(const std::string& text) { return search(text, balanceRe()); }

Json weaponChanges(const std::string& text, const std::vector<std::string>& weapons) {
    struct Rec { std::string weapon; int buff = 0, nerf = 0; std::vector<std::string> detail; };
    std::vector<Rec> out;
    auto rec = [&](const std::string& w) -> Rec& {
        for (auto& r : out) if (r.weapon == w) return r;
        Rec r;
        r.weapon = w;
        out.push_back(r);
        return out.back();
    };
    auto named = [&](const std::string& s) {
        std::vector<std::string> v;
        for (auto& w : weapons) if (containsWord(s, w)) v.push_back(w);
        return v;
    };
    static const std::regex clauses = re(R"(,|;|\bwhile\b|\bbut\b|\bwhereas\b|\band\b(?=\s+(?:the\s+)?[A-Z]))");
    std::string current;
    for (auto raw : splitSentences(text)) {
        std::string line = stripBullets(raw);
        if (line.empty()) continue;
        auto n = named(line);
        if (!n.empty() && line.size() < 60 && !(search(line, buffRe()) || search(line, nerfRe()))) {
            current = n.front();  // a heading like "M4" with its changes listed under it
            continue;
        }
        std::string last = current;
        for (auto it = std::sregex_token_iterator(line.begin(), line.end(), clauses, -1); it != std::sregex_token_iterator(); ++it) {
            std::string part = *it;
            auto inPart = named(part);
            std::vector<std::string> targets = !inPart.empty() ? inPart : (last.empty() ? std::vector<std::string>{} : std::vector<std::string>{last});
            if (!inPart.empty()) last = inPart.back();
            bool buff = search(part, buffRe()), nerf = search(part, nerfRe());
            if (targets.empty() || !(buff || nerf)) continue;
            for (auto& w : targets) {
                Rec& r = rec(w);
                r.buff += buff;
                r.nerf += nerf;
                std::string d = truncateUtf8(line, 160);
                if (r.detail.size() < 4 && std::find(r.detail.begin(), r.detail.end(), d) == r.detail.end()) r.detail.push_back(d);
            }
        }
    }
    Json res = Json::array();
    for (auto& r : out) {
        Json o = Json::object();
        o["weapon"] = r.weapon;
        o["direction"] = r.buff > r.nerf ? "buff" : r.nerf > r.buff ? "nerf" : "changed";
        Json d = Json::array();
        for (auto& x : r.detail) d.push(x);
        o["detail"] = d;
        res.push(o);
    }
    return res;
}

std::string parsePatchDate(const std::string& textIn) {
    std::string text = trim(textIn);
    if (text.empty()) return "";
    // ISO 8601
    if (text.size() >= 10 && std::isdigit(static_cast<unsigned char>(text[0])) && text[4] == '-') {
        std::string t = text;
        if (t.size() == 10) t += "T00:00:00+00:00";
        if (t.back() == 'Z') t = t.substr(0, t.size() - 1) + "+00:00";
        auto v = parseIso(t);
        return v ? utcIso(*v) : "";
    }
    // RFC 822: "Mon, 23 Nov 2026 10:00:00 GMT" / "+0000"
    std::string s = text;
    size_t comma = s.find(',');
    if (comma != std::string::npos) s = trim(s.substr(comma + 1));
    char mon[8] = {0}, zone[16] = {0};
    int d = 0, y = 0, h = 0, mi = 0, sec = 0;
    int n = std::sscanf(s.c_str(), "%d %3s %d %d:%d:%d %15s", &d, mon, &y, &h, &mi, &sec, zone);
    if (n < 6) {
        sec = 0;
        n = std::sscanf(s.c_str(), "%d %3s %d %d:%d %15s", &d, mon, &y, &h, &mi, zone);
        if (n < 5) return "";
    }
    static const char* months[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    int m = 0;
    for (int i = 0; i < 12; ++i) if (lower(mon) == months[i]) m = i + 1;
    if (!m) return "";
    if (y < 100) y += 2000;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d", y, m, d, h, mi, sec);
    std::string z = zone;
    std::string off = "+00:00";
    if ((z.size() == 5) && (z[0] == '+' || z[0] == '-')) off = z.substr(0, 3) + ":" + z.substr(3);
    else if (z == "EST") off = "-05:00";
    else if (z == "EDT") off = "-04:00";
    else if (z == "PST") off = "-08:00";
    else if (z == "PDT") off = "-07:00";
    auto v = parseIso(std::string(buf) + off);
    return v ? utcIso(*v) : "";
}

Json recentPatches(Store& store, const std::string& gameId, int days) {
    Json rows = store.patchesSince(gameId, utcIso(static_cast<std::time_t>(nowSeconds() - days * 86400LL)));
    for (auto& r : rows.elements()) {
        try { r["weapon_changes"] = Json::parse(jstr(r, "weapon_changes", "[]")); } catch (...) { r["weapon_changes"] = Json::array(); }
    }
    return rows;
}

Json buildFreshness(Store& store, const std::string& gameId, const std::string& weapon, const std::string& asOfIn) {
    auto result = [](const char* state, const std::string& why, const std::string& dir) {
        Json o = Json::object();
        o["state"] = state;
        o["why"] = why;
        o["direction"] = dir;
        return o;
    };
    if (asOfIn.empty()) return result("current", "", "");
    std::string asOf = asOfIn.find('T') != std::string::npos ? asOfIn : asOfIn + "T23:59:59+00:00";
    std::string key = normKey(weapon);
    std::vector<Json> later;
    const Json recent = recentPatches(store, gameId, 365);
    for (auto& p : recent.elements()) {
        std::string when = jstr(p, "published").empty() ? jstr(p, "seen_at") : jstr(p, "published");
        if (when > asOf) later.push_back(p);
    }
    for (auto& p : later)
        for (auto& ch : p.get("weapon_changes").elements())
            if (normKey(jstr(ch, "weapon")) == key) {
                std::string when = (jstr(p, "published").empty() ? jstr(p, "seen_at") : jstr(p, "published")).substr(0, 10);
                return result("outdated", jstr(ch, "direction") + " on " + when + ": " + truncateUtf8(jstr(p, "title"), 80),
                              jstr(ch, "direction"));
            }
    const Json changes = store.statChanges(gameId, asOf);
    for (auto& c : changes.elements())
        if (normKey(jstr(c, "weapon")) == key)
            return result("outdated", jstr(c, "field") + " changed on " + jstr(c, "source") + " (" + jstr(c, "seen_at").substr(0, 10) + ")",
                          "changed");
    for (auto& p : later)
        if (jinteger(p, "balance")) return result("check", "balance patch since this build: " + truncateUtf8(jstr(p, "title"), 80), "");
    return result("current", "", "");
}

PatchWatcher::PatchWatcher(Config& cfg, Store& store, HttpClient& http, std::function<void(const std::string&)> onLine)
    : cfg_(cfg), store_(store), http_(http), onLine_(std::move(onLine)) {}

bool PatchWatcher::save(const std::string& gameId, const std::string& title, const std::string& url, const std::string& published,
                        const Json& changes, bool balance) {
    if (store_.insertPatch(gameId, title, url, published, changes.dump(), balance)) return true;
    if (changes.size()) {  // seen before: keep the richest reading
        Json old = store_.patch(gameId, url);
        size_t had = 0;
        try { had = Json::parse(jstr(old, "weapon_changes", "[]")).size(); } catch (...) {}
        if (had < changes.size()) store_.updatePatchChanges(gameId, url, changes.dump(), balance);
    }
    return false;
}

Json PatchWatcher::check(const std::string& gameId) {
    const Game& g = gameById(gameId);
    Json cfg = cfg_.snapshot();
    MetaData meta(cfg_.paths());
    SourceCatalog catalog(cfg_.paths());
    ScreenshotLibrary shots(cfg_.paths());
    Researcher r(cfg, http_, meta, catalog, shots, nullptr, onLine_);
    r.renderer = renderer;
    auto weapons = meta.knownWeapons();
    Json fresh = Json::array();
    auto add = [&](const std::string& title, const std::string& url, const Json& changes) {
        Json o = Json::object();
        o["title"] = title;
        o["url"] = url;
        o["changes"] = changes;
        fresh.push(o);
    };
    std::vector<std::string> pages;
    for (auto& u : cfg.get("research").get("patches_pages").get(gameId).elements()) pages.push_back(u.toString());
    if (gameId != "codm") pages.push_back(kOfficialPatchNotes);
    for (auto& url : pages) {
        std::string html, finalUrl;
        try {
            std::tie(html, finalUrl) = r.fetch(url);
        } catch (const std::exception& e) {
            if (onLine_) onLine_("Patch notes " + urlDomain(url) + ": " + truncateUtf8(e.what(), 90));
            continue;
        }
        Document doc = htmlToDocument(html, finalUrl);
        std::vector<std::string> lines;
        for (auto& b : doc.blocks) lines.push_back(b.heading + ": " + b.text);
        std::string text = join(lines, "\n");
        if (g.id != "codm") {  // the official page covers several games: keep it only when it covers this one
            bool covers = icontains(text, g.name);
            for (auto& a : g.aliases) covers = covers || icontains(text, a);
            if (!covers) continue;
        }
        Json changes = weaponChanges(text, weapons);
        if (save(gameId, doc.title.empty() ? g.label() + " patch notes" : doc.title, finalUrl, "", changes,
                 changes.size() > 0 || mentionsBalance(text)))
            add(doc.title, finalUrl, changes);
    }
    if (cfg.get("patches").get("news_feed").toBool(true)) {
        std::string query = g.id != "codm" ? "\"" + g.name + "\" patch notes" : "\"COD Mobile\" patch notes";
        HttpResponse resp = http_.get(googleNewsUrl(query, static_cast<int>(jinteger(cfg.get("patches"), "news_days", 14))),
                                      {{"User-Agent", "COD-Redemption-Hunter/6"}}, 20);
        static const std::regex tags = re("<[^>]+>");
        if (resp.status == 200)
            for (auto& it : parseFeedItems(resp.body)) {
                std::string text = it.title + "\n" + std::regex_replace(it.summary, tags, " ");
                if (!icontains(text, "patch") && !mentionsBalance(text)) continue;
                Json changes = weaponChanges(text, weapons);
                if (save(gameId, it.title, it.link, parsePatchDate(it.published), changes, changes.size() > 0 || mentionsBalance(text)))
                    add(it.title, it.link, changes);
            }
    }
    store_.setState("patch_checked:" + gameId, utcNowIso());
    return fresh;
}

Json PatchWatcher::runDue(const std::function<void(const std::string&, const std::string&, const std::string&)>& notify) {
    Json cfg = cfg_.snapshot();
    const Json& pc = cfg.get("patches");
    double every = jnum(pc, "check_hours", 6);
    std::vector<std::string> gameIds;
    for (auto& g : pc.get("games").elements()) gameIds.push_back(g.toString());
    if (gameIds.empty()) gameIds = kCurrent;
    Json found = Json::object();
    for (auto& gid : gameIds) {
        auto last = parseIso(store_.getState("patch_checked:" + gid));
        if (last && static_cast<double>(nowSeconds() - *last) < every * 3600) continue;
        Json fresh = Json::array();
        const Json found2 = check(gid);
        for (auto& p : found2.elements()) if (p.get("changes").size()) fresh.push(p);
        if (!fresh.size()) continue;
        found[gid] = fresh;
        std::set<std::string> names;
        for (auto& p : fresh.elements()) for (auto& c : p.get("changes").elements()) names.insert(jstr(c, "weapon"));
        std::vector<std::string> v(names.begin(), names.end());
        std::string list = join(std::vector<std::string>(v.begin(), v.begin() + std::min<size_t>(8, v.size())), ", ");
        std::string msg = gameById(gid).label() + ": balance patch — " + list + (v.size() > 8 ? "…" : "");
        if (notify) notify("Meta changed", msg, jstr(fresh.elements()[0], "url"));
        store_.logScan("Patch watch", "OK", msg);
        if (pc.get("auto_research").toBool(true)) {
            try {
                AiClient ai(http_, cfg_.env("AI_PROVIDER"), cfg_.env("AI_API_KEY"), cfg_.env("AI_MODEL"), cfg_.env("AI_BASE_URL"));
                MetaData meta(cfg_.paths());
                SourceCatalog catalog(cfg_.paths());
                ScreenshotLibrary shots(cfg_.paths());
                Researcher r(cfg, http_, meta, catalog, shots, ai.ready() ? &ai : nullptr, onLine_);
                r.renderer = renderer;
                r.researchBuilds(gid);
            } catch (const std::exception& e) {
                if (onLine_) onLine_(std::string("Auto re-research failed: ") + e.what());
            }
        }
    }
    return found;
}

}  // namespace hunter
