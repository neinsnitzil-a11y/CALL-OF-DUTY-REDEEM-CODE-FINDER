#include "helper.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <regex>
#include <set>

#include "games.hpp"
#include "html.hpp"
#include "util.hpp"

namespace hunter {

namespace {
const char* kSystem =
    "You are the AI helper inside COD Redemption Hunter. Answer ANY question the user asks — gaming or not — "
    "like a knowledgeable, friendly assistant, in a natural chat style. You're also a Call of Duty expert "
    "(builds, settings, codes, meta, every title incl. COD Mobile). Be accurate: when sources or live web "
    "results are provided, base time-sensitive facts on them and cite them like [1]; if you're unsure or "
    "the info may be outdated, say so. Never invent redeem codes. Never help bypass CAPTCHAs, rate limits, "
    "anti-cheat or terms of service. Use short paragraphs and lists; Markdown **bold** and `code` are fine.";
const char* kUA = "COD-Redemption-Hunter/6 (open-source; AI helper lookups)";

const std::regex& codRe() {
    static const std::regex r = re(
        R"(\b(?:cod|codm|call of duty|warzone|black ops|modern warfare|mw ?[234]|mwii+|bo ?[1-7]|loadouts?|)"
        R"(class setups?|attachments?|gunsmith|redeem codes?|redemption codes?|camos?|prestige|sbmm|ttk|)"
        R"(time to kill|aim assist|deadzones?|resurgence|verdansk|rebirth island|dmz|cdl|meta (?:build|gun|)"
        R"(loadout|weapon)s?)\b)",
        true);
    return r;
}

std::string todayLong() {
    std::time_t t = static_cast<std::time_t>(nowSeconds());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof buf, "%A %d %B %Y", &tm);
    return buf;
}
}  // namespace

// ── free lookups ────────────────────────────────────────────────
std::vector<WebItem> newsSearch(HttpClient& http, const std::string& query, size_t limit) {
    HttpResponse r = http.get(googleNewsUrl(query, 0), {{"User-Agent", kUA}}, 15);
    std::vector<WebItem> out;
    if (r.status != 200) return out;
    static const std::regex tags = re("<[^>]+>");
    for (auto& it : parseFeedItems(r.body)) {
        if (out.size() >= limit) break;
        out.push_back({it.title, it.link, truncateUtf8(collapseSpaces(std::regex_replace(it.summary, tags, " ")), 400), it.published});
    }
    return out;
}

std::vector<WebItem> wikipediaSearch(HttpClient& http, const std::string& query, size_t limit, const std::string& lang) {
    std::vector<WebItem> out;
    std::string host = "https://" + lang + ".wikipedia.org";
    HttpResponse r = http.get(host + "/w/rest.php/v1/search/page?q=" + urlEncode(query) + "&limit=" + std::to_string(limit),
                              {{"User-Agent", kUA}}, 15);
    if (r.status != 200) return out;
    Json pages;
    try { pages = Json::parse(r.body).get("pages"); } catch (...) { return out; }
    for (const auto& p : pages.elements()) {
        if (out.size() >= limit) break;
        std::string key = jstr(p, "key", replaceAll(jstr(p, "title"), " ", "_"));
        HttpResponse s = http.get(host + "/api/rest_v1/page/summary/" + replaceAll(urlEncode(key), "+", "%20"), {{"User-Agent", kUA}}, 15);
        if (s.status != 200) continue;
        try {
            Json d = Json::parse(s.body);
            std::string url = jstr(d.get("content_urls").get("desktop"), "page", host + "/wiki/" + key);
            out.push_back({jstr(d, "title", key), url, truncateUtf8(jstr(d, "extract"), 1500), ""});
        } catch (...) {}
    }
    return out;
}

std::vector<std::string> urlsIn(const std::string& text, size_t limit) {
    static const std::regex rx = re(R"(https://[^\s<>"')\]]+)");
    std::vector<std::string> out;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it) {
        std::string u = it->str();
        while (!u.empty() && (u.back() == '.' || u.back() == ',')) u.pop_back();
        if (std::find(out.begin(), out.end(), u) == out.end() && out.size() < limit) out.push_back(u);
    }
    return out;
}

// ── helper ──────────────────────────────────────────────────────
Helper::Helper(Config& cfg, Store& store, HttpClient& http)
    : meta(cfg.paths()), catalog(cfg.paths()), shots(cfg.paths()), research(cfg.paths()), cfg_(cfg), store_(store), http_(http) {}

std::vector<std::string> Helper::weaponsIn(const std::string& text, const std::string& gameIdIn) const {
    std::string gameId = gameIdIn.empty() ? "warzone" : gameIdIn;
    std::set<std::string> names;
    const Json curated = meta.forGame(gameId), researched = research.forGame(gameId);
    for (auto& b : curated.get("builds").elements()) names.insert(jstr(b, "weapon"));
    for (auto& r : researched.get("builds").get("weapons").elements()) names.insert(jstr(r, "weapon"));
    for (auto& m : shots.listing(gameId)) if (!jstr(m, "weapon").empty()) names.insert(jstr(m, "weapon"));
    std::vector<std::string> v(names.begin(), names.end());
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.size() > b.size(); });
    std::vector<std::string> out;
    for (auto& n : v) if (!n.empty() && containsWord(text, n)) out.push_back(n);
    return out;
}

bool Helper::isCod(const std::string& q, const std::string& gameId) const {
    return search(q, codRe()) || !weaponsIn(q, gameId).empty();
}

std::string Helper::appContext(const std::string& gameIdIn) const {
    std::string gameId = gameIdIn.empty() ? "warzone" : gameIdIn;
    const Game& g = gameById(gameId);
    std::vector<std::string> lines{"Game: " + g.name};
    std::vector<std::string> codes;
    for (auto& r : store_.codesView("ready", "", gameId)) if (codes.size() < 10) codes.push_back(r.code);
    lines.push_back("Working codes in the app: " + (codes.empty() ? std::string("none right now") : join(codes, ", ")));
    std::vector<std::string> builds;
    const Json curated = meta.forGame(g.id), researched = research.forGame(g.id);
    for (auto& b : curated.get("builds").elements()) if (builds.size() < 8) builds.push_back(buildText(b));
    lines.push_back("Curated builds:\n" + join(builds, "\n"));
    std::vector<std::string> rs;
    for (auto& r : researched.get("builds").get("weapons").elements()) if (rs.size() < 6) rs.push_back(researchText(r));
    if (!rs.empty()) lines.push_back("Researched (cross-checked) builds:\n" + join(rs, "\n"));
    return truncateUtf8(join(lines, "\n"), 9000);
}

void Helper::codPages(Researcher& r, const std::string& question, const std::string& gameId, std::vector<std::string>& blocks,
                      const std::function<int(const std::string&, const std::string&)>& source,
                      const std::function<void(const Json&)>& show, const HelperEvent&) {
    std::string q = lower(question);
    auto any = [&](std::initializer_list<const char*> w) {
        return std::any_of(w.begin(), w.end(), [&](const char* x) { return q.find(x) != std::string::npos; });
    };
    std::string kind = any({"setting", "sensitiv", "fov", "graphic", "deadzone", "aim assist", "controller", "keybind", "dpi", "fps"})
                           ? "settings"
                       : any({"ttk", "time to kill", "recoil", "damage", "stats", "velocity"}) ? "stats" : "builds";
    auto known = meta.knownWeapons();
    Json cfg = cfg_.snapshot();
    bool saveShots = cfg.get("research").get("save_screenshots").toBool(true);
    r.walk(gameId, kind, [&](const std::string& html, const std::string& finalUrl, const Researcher::PlanEntry& e) {
        int n = source(finalUrl, e.name);
        blocks.push_back("[" + std::to_string(n) + "] " + finalUrl + "\n" + docText(htmlToDocument(html, finalUrl), 4000));
        if (!saveShots) return std::string();
        for (auto& [imgUrl, alt] : pageImages(html, finalUrl, 2, kind == "settings")) {
            auto img = r.image(imgUrl);
            if (!img) continue;
            std::string label = alt + " " + replaceAll(imgUrl.substr(imgUrl.rfind('/') + 1), "-", " ");
            std::string weapon;
            if (kind != "settings") for (auto& w : known) if (containsWord(label, w)) { weapon = w; break; }
            try {
                show(shots.save(gameId, img->bytes, img->mime, weapon, finalUrl, imgUrl, alt, "research", "",
                                jnum(cfg.get("research"), "screenshot_library_mb", 500), kind == "settings" ? "settings" : "build"));
            } catch (...) {}
        }
        return std::string();
    }, 3);
}

Json Helper::ask(AiClient& ai, const std::string& question, const std::string& gameIdIn, bool web, const AiHistory& history,
                 const std::vector<AiImage>& images, const HelperEvent& onEvent) {
    std::string gameId = gameIdIn.empty() ? "warzone" : gameIdIn;
    bool cod = isCod(question, gameId);
    std::vector<std::pair<std::string, std::string>> sources;
    std::vector<std::string> blocks;
    Json shown = Json::array();
    std::set<std::string> shownFiles;
    auto show = [&](const Json& m) {
        if (m.isObject() && shownFiles.insert(jstr(m, "file")).second) {
            shown.push(m);
            onEvent("image", m);
        }
    };
    auto source = [&](const std::string& url, const std::string& title) -> int {
        if (url.empty()) return 0;
        for (size_t i = 0; i < sources.size(); ++i) if (sources[i].first == url) return static_cast<int>(i + 1);
        sources.emplace_back(url, title);
        Json s = Json::array();
        s.push(url);
        s.push(title);
        onEvent("source", s);
        return static_cast<int>(sources.size());
    };
    auto line = [&](const std::string& t) { onEvent("line", Json(t)); };

    if (cod) {  // screenshots you already have for the guns you asked about
        auto ws = weaponsIn(question, gameId);
        for (size_t i = 0; i < ws.size() && i < 3; ++i) {
            auto list = shots.listing(gameId, ws[i]);
            for (size_t k = 0; k < list.size() && k < 2; ++k) show(list[k]);
        }
    }
    Json cfg = cfg_.snapshot();
    Researcher r(cfg, http_, meta, catalog, shots, nullptr, line);
    r.renderer = renderer;
    r.stop = ai.cancel;
    if (sleeper) r.sleeper = sleeper;
    for (auto& url : urlsIn(question)) {  // links you pasted, any topic
        try {
            auto [html, finalUrl] = r.fetch(url);
            line("Read " + urlDomain(finalUrl));
            blocks.push_back("[" + std::to_string(source(finalUrl, "")) + "] " + finalUrl + "\n" +
                             docText(htmlToDocument(html, finalUrl), 6000));
        } catch (const std::exception& e) {
            blocks.push_back("[" + std::to_string(source(url, "")) + "] " + url + "\n(couldn't read this page: " +
                             truncateUtf8(e.what(), 120) + ")");
        }
    }
    std::string native = web ? ai.nativeSearch() : "";
    auto fallbackLookup = [&] {
        if (cod) codPages(r, question, gameId, blocks, source, show, onEvent);
        line("🔎 Searching the news and Wikipedia…");
        for (auto& n : newsSearch(http_, question, 5))
            blocks.push_back("[" + std::to_string(source(n.url, n.title)) + "] " + n.title + " (" + n.date.substr(0, 16) + ") — " + n.text);
        for (auto& w : wikipediaSearch(http_, question, 2))
            blocks.push_back("[" + std::to_string(source(w.url, w.title)) + "] Wikipedia: " + w.title + " — " + w.text);
    };
    if (web && native.empty()) fallbackLookup();
    else if (!native.empty()) line("🌐 Live web: " + native + " (the AI searches when it needs to)");

    auto prompt = [&] {
        std::string p = "Today is " + todayLong() + ".\n";
        if (cod) p += "What the app knows:\n" + appContext(gameId) + "\n\n";
        if (!blocks.empty()) p += "Sources:\n" + join(blocks, "\n\n") + "\n\n";
        if (!images.empty()) p += "The user attached an image — look at it first.\n";
        return p + "Question: " + question;
    };
    onEvent("status", Json("typing…"));
    auto onText = [&](const std::string& t) { onEvent("text", Json(t)); };
    auto onSource = [&](const std::string& u, const std::string& t) { source(u, t); };
    auto onStatus = [&](const std::string& m) { onEvent("status", Json(m)); };
    std::string answer;
    try {
        answer = ai.chatStream(prompt(), onText, kSystem, images, 2000, history, !native.empty(), onSource, onStatus);
    } catch (const AiError& e) {
        if (native.empty() || std::string(e.what()).find("HTTP 4") == std::string::npos) throw;
        // the key/plan doesn't allow the provider's web search: use the free lookup instead
        line(native + " isn't available on this key — using the free lookup instead.");
        native.clear();
        fallbackLookup();
        answer = ai.chatStream(prompt(), onText, kSystem, images, 2000, history);
    }
    if (cod) {  // screenshots of the guns the answer recommends
        auto ws = weaponsIn(answer, gameId);
        for (size_t i = 0; i < ws.size() && i < 4; ++i) {
            auto list = shots.listing(gameId, ws[i]);
            if (!list.empty()) show(list.front());
        }
    }
    Json res = Json::object();
    res["answer"] = answer;
    Json srcs = Json::array(), titles = Json::object();
    for (auto& [u, t] : sources) { srcs.push(u); titles[u] = t; }
    res["sources"] = srcs;
    res["source_titles"] = titles;
    res["images"] = shown;
    res["search"] = !native.empty() ? native : (web ? "free lookup" : "off");
    return res;
}

Json Helper::loadChat() const {
    try {
        Json d = Json::parse(readFile(cfg_.paths().dataDir / "data" / "chat_history.json"));
        return d.isArray() ? d : Json::array();
    } catch (...) {
        return Json::array();
    }
}

void Helper::saveChat(const Json& turns) const {
    Json keep = Json::array();
    const auto& all = turns.elements();
    for (size_t i = all.size() > 60 ? all.size() - 60 : 0; i < all.size(); ++i) keep.push(all[i]);
    writeFileAtomic(cfg_.paths().dataDir / "data" / "chat_history.json", keep.dump());
}

Json Helper::saveAnswer(const std::string& question, const Json& result, const std::string& gameId) const {
    Json e = Json::object();
    e["saved_at"] = utcNowIso();
    e["game"] = gameId;
    e["question"] = question;
    e["answer"] = jstr(result, "answer");
    e["sources"] = result.get("sources").isArray() ? result.get("sources") : Json::array();
    Json imgs = Json::array();
    for (auto& m : result.get("images").elements()) imgs.push(jstr(m, "file"));
    e["images"] = imgs;
    fs::path p = cfg_.paths().dataDir / "data" / "ai_answers.jsonl";
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::app | std::ios::binary);
    out << e.dump() << "\n";
    return e;
}

std::vector<Json> Helper::savedAnswers() const {
    std::vector<Json> out;
    for (auto& l : splitLines(readFile(cfg_.paths().dataDir / "data" / "ai_answers.jsonl"))) {
        if (trim(l).empty()) continue;
        try { out.push_back(Json::parse(l)); } catch (...) {}
    }
    std::reverse(out.begin(), out.end());
    return out;
}

}  // namespace hunter
