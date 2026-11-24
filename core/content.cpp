#include "content.hpp"

#include <algorithm>
#include <fstream>
#include <random>
#include <set>

#include "util.hpp"
#include "games.hpp"

namespace hunter {

std::string normKey(const std::string& s) {
    std::string out;
    for (char c : s) if (isAsciiAlnum(c)) out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// ── meta ────────────────────────────────────────────────────────
bool MetaData::valid(const Json& d) {
    if (!d.isObject() || !d.get("games").isObject() || !d.get("updated").isString()) return false;
    if (!d.get("settings").isNull() && !d.get("settings").isObject()) return false;
    for (const auto& [id, g] : d.get("games").items()) {
        if (!g.isObject() || (!g.get("builds").isNull() && !g.get("builds").isArray())) return false;
        for (const auto& b : g.get("builds").elements())
            if (!b.isObject() || !b.get("weapon").isString()) return false;
    }
    return true;
}

Json MetaData::load() const {
    auto read = [](const fs::path& p) -> Json {
        try {
            Json d = Json::parse(readFile(p));
            return valid(d) ? d : Json();
        } catch (...) {
            return Json();
        }
    };
    Json bundled = read(paths_.appDir / "meta.json");
    if (bundled.isNull()) bundled = Json::parse(R"({"updated":"","games":{},"settings":{}})");
    Json update = read(paths_.dataDir / "data" / "meta_update.json");
    return !update.isNull() && jstr(update, "updated") > jstr(bundled, "updated") ? update : bundled;
}

Json MetaData::forGame(const std::string& gameId) const {
    Json d = load();
    const Json& g = d.get("games").get(gameId);
    std::string key = jstr(g, "settings", gameId == "codm" ? "codm" : "modern");
    Json out = Json::object();
    out["season"] = jstr(g, "season");
    out["note"] = jstr(g, "note");
    out["builds"] = g.get("builds").isArray() ? g.get("builds") : Json::array();
    out["live"] = g.get("live").isArray() ? g.get("live") : Json::array();
    out["settings"] = d.get("settings").get(key).isObject() ? d.get("settings").get(key) : Json::object();
    out["updated"] = jstr(d, "updated");
    return out;
}

std::vector<std::string> MetaData::knownWeapons() const {
    std::set<std::string> names;
    Json d = load();
    for (const auto& [id, g] : d.get("games").items())
        for (const auto& b : g.get("builds").elements()) names.insert(jstr(b, "weapon"));
    std::vector<std::string> out(names.begin(), names.end());
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.size() > b.size(); });
    return out;
}

std::pair<bool, std::string> MetaData::checkForUpdate(HttpClient& http, const std::string& url) const {
    if (url.empty()) return {false, "No update address set (config → meta.update_url)."};
    if (!startsWith(lower(url), "https://")) return {false, "The update address must start with https://"};
    HttpResponse r = http.get(url, {{"User-Agent", "COD-Redemption-Hunter"}}, 20);
    if (!r.ok()) return {false, "Couldn't fetch builds update: " + (r.status ? "HTTP " + std::to_string(r.status) : r.error)};
    if (r.body.size() > 2000000) return {false, "Update file is too large."};
    Json data;
    try { data = Json::parse(r.body); } catch (...) { return {false, "The downloaded file isn't a valid builds file."}; }
    if (!valid(data)) return {false, "The downloaded file isn't a valid builds file."};
    Json current = load();
    if (jstr(data, "updated") <= jstr(current, "updated"))
        return {false, "Already up to date (builds from " + jstr(current, "updated") + ")."};
    writeFileAtomic(paths_.dataDir / "data" / "meta_update.json", data.dump());
    return {true, "Builds updated to " + jstr(data, "updated") + "."};
}

std::string buildText(const Json& b, const std::string& gameLabel) {
    std::vector<std::string> lines;
    lines.push_back(jstr(b, "weapon") + (jstr(b, "class").empty() ? "" : " (" + jstr(b, "class") + ")") +
                    (gameLabel.empty() ? "" : " — " + gameLabel));
    if (!jstr(b, "role").empty())
        lines.push_back("Role: " + jstr(b, "role") + (jstr(b, "tier").empty() ? "" : " · Tier " + jstr(b, "tier")));
    for (const auto& a : b.get("attachments").elements())
        if (a.size() >= 2) lines.push_back("  " + a.elements()[0].toString() + ": " + a.elements()[1].toString());
    auto list = [](const Json& arr) {
        std::vector<std::string> v;
        for (const auto& x : arr.elements()) v.push_back(x.toString());
        return join(v, ", ");
    };
    if (b.get("perks").size()) lines.push_back("Perks: " + list(b.get("perks")));
    if (b.get("equipment").size()) lines.push_back("Equipment: " + list(b.get("equipment")));
    if (!jstr(b, "secondary").empty()) lines.push_back("Secondary: " + jstr(b, "secondary"));
    if (!jstr(b, "source").empty())
        lines.push_back(trim("Source: " + jstr(b, "source") + " (" + jstr(b, "as_of") + ") " + jstr(b, "source_url")));
    return join(lines, "\n");
}

// ── catalog ─────────────────────────────────────────────────────
Json SourceCatalog::load() const {
    try {
        return Json::parse(readFile(paths_.appDir / "research_sources.json"));
    } catch (...) {
        return Json::parse(R"({"sources":[],"titles":{}})");
    }
}

std::vector<Json> SourceCatalog::all(const Json& cfg) const {
    const Json& r = cfg.get("research");
    std::set<std::string> off, on;
    for (const auto& x : r.get("disabled_sources").elements()) off.insert(x.toString());
    for (const auto& x : r.get("enabled_sources").elements()) on.insert(x.toString());
    std::vector<Json> out;
    Json cat = load();
    auto add = [&](Json s, bool custom) {
        bool enabled = (s.get("enabled").toBool(true) || on.count(jstr(s, "name"))) && !off.count(jstr(s, "name"));
        s["enabled"] = enabled;
        if (custom) s["custom"] = true;
        out.push_back(s);
    };
    for (const auto& s : cat.get("sources").elements()) add(s, false);
    for (const auto& s : r.get("custom_sources").elements()) add(s, true);
    return out;
}

std::vector<CatalogEntry> SourceCatalog::forGame(const Json& cfg, const std::string& gameId, const std::string& kind) const {
    Json cat = load();
    const Json& titles = cat.get("titles").get(gameId);
    std::vector<CatalogEntry> out;
    for (const auto& s : all(cfg)) {
        if (!jbool(s, "enabled")) continue;
        const auto& games = s.get("games").elements();
        bool forThis = games.empty();
        for (const auto& g : games) if (g.toString() == gameId || g.toString() == "all") forThis = true;
        if (!forThis) continue;
        const Json& kinds = s.get("kinds");
        bool kindOk = false;
        if (!kinds.isArray()) kindOk = kind == "builds";
        for (const auto& k : kinds.elements()) if (k.toString() == kind) kindOk = true;
        if (!kindOk) continue;
        std::string url = jstr(s, "url");
        bool specific = url.find('{') != std::string::npos;
        if (specific) {  // {fandom} {ign} {steam} {pcgw} → this game's page; skip sites with no page for it
            bool missing = false;
            std::string filled;
            for (size_t i = 0; i < url.size(); ++i) {
                if (url[i] != '{') { filled += url[i]; continue; }
                size_t close = url.find('}', i);
                if (close == std::string::npos) { missing = true; break; }
                std::string key = url.substr(i + 1, close - i - 1);
                if (!titles.contains(key)) { missing = true; break; }
                filled += titles.get(key).toString();
                i = close;
            }
            if (missing) continue;
            url = filled;
        }
        if (!startsWith(url, "https://")) continue;
        out.push_back({jstr(s, "name"), url, jstr(s, "note"), jbool(s, "crawl"), specific || games.size() == 1,
                       static_cast<int>(jinteger(s, "priority", 2))});
    }
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.priority < b.priority; });
    return out;
}

// ── screenshots ─────────────────────────────────────────────────
std::string ScreenshotLibrary::extFor(const std::string& mime) {
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "image/webp") return ".webp";
    if (mime == "image/gif") return ".gif";
    return "";
}
std::string ScreenshotLibrary::mimeFor(const fs::path& p) {
    std::string e = lower(p.extension().string());
    if (e == ".png") return "image/png";
    if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
    if (e == ".webp") return "image/webp";
    if (e == ".gif") return "image/gif";
    return "";
}

Json ScreenshotLibrary::save(const std::string& gameId, const std::string& bytes, const std::string& mime,
                             const std::string& weapon, const std::string& sourcePage, const std::string& imageUrl,
                             const std::string& alt, const std::string& addedBy, const std::string& note, double capMb,
                             const std::string& kind) {
    std::string ext = extFor(mime);
    if (ext.empty()) throw std::invalid_argument("unsupported image type " + mime);
    std::string digest = sha256Hex(bytes).substr(0, 20);
    fs::path folder = root_ / (gameId.empty() ? "other" : gameId) / (addedBy != "research" ? "community" : "web");
    fs::create_directories(folder);
    fs::path path = folder / (digest + ext);
    fs::path metaPath = folder / (digest + ".json");
    Json meta = Json::object();
    try { if (fs::exists(metaPath)) meta = Json::parse(readFile(metaPath)); } catch (...) { meta = Json::object(); }
    if (!fs::exists(path)) {
        std::ofstream out(path, std::ios::binary);
        out << bytes;
    }
    auto keep = [&](const char* k, const std::string& v) { meta[k] = v.empty() ? jstr(meta, k) : v; };
    meta["file"] = path.string();
    meta["game"] = gameId;
    keep("weapon", weapon);
    keep("source_page", sourcePage);
    keep("image_url", imageUrl);
    meta["site"] = sourcePage.empty() ? jstr(meta, "site") : urlDomain(sourcePage);
    keep("alt", alt);
    meta["added_by"] = jstr(meta, "added_by", addedBy);
    keep("note", note);
    meta["saved_at"] = jstr(meta, "saved_at", utcNowIso());
    meta["bytes"] = static_cast<long long>(bytes.size());
    meta["mime"] = mime;
    meta["kind"] = jstr(meta, "kind", kind);
    writeFileAtomic(metaPath, meta.dump(1));
    prune(capMb);
    return meta;
}

Json ScreenshotLibrary::addFile(const std::string& gameId, const fs::path& file, const std::string& weapon, const std::string& note) {
    std::string mime = mimeFor(file);
    if (mime.empty()) throw std::invalid_argument("Use a PNG, JPG, WEBP or GIF image.");
    std::string data = readFile(file);
    if (data.size() > 15000000) throw std::invalid_argument("That image is over 15 MB.");
    return save(gameId, data, mime, weapon, "", "", "", "you", note.empty() ? file.filename().string() : note);
}

std::vector<Json> ScreenshotLibrary::listing(const std::string& gameId, const std::string& weapon) const {
    fs::path base = gameId.empty() ? root_ : root_ / gameId;
    std::vector<Json> out;
    std::error_code ec;
    if (!fs::exists(base, ec)) return out;
    for (auto it = fs::recursive_directory_iterator(base, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file() || it->path().extension() != ".json") continue;
        try {
            Json m = Json::parse(readFile(it->path()));
            if (!fs::exists(jstr(m, "file"))) continue;
            if (!weapon.empty() && normKey(jstr(m, "weapon")) != normKey(weapon)) continue;
            out.push_back(m);
        } catch (...) {}
    }
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return jstr(a, "saved_at") > jstr(b, "saved_at"); });
    return out;
}

void ScreenshotLibrary::setWeapon(Json& meta, const std::string& weapon) {
    meta["weapon"] = weapon;
    fs::path p = jstr(meta, "file");
    writeFileAtomic(p.replace_extension(".json"), meta.dump(1));
}

void ScreenshotLibrary::remove(const Json& meta) {
    std::error_code ec;
    fs::path p = jstr(meta, "file");
    fs::remove(p, ec);
    fs::remove(p.replace_extension(".json"), ec);
}

double ScreenshotLibrary::sizeMb() const {
    std::error_code ec;
    if (!fs::exists(root_, ec)) return 0;
    uintmax_t total = 0;
    for (auto it = fs::recursive_directory_iterator(root_, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file()) total += it->file_size(ec);
    }
    return static_cast<double>(total) / 1e6;
}

int ScreenshotLibrary::prune(double capMb) {
    if (sizeMb() <= capMb) return 0;
    std::vector<Json> web;
    for (auto& m : listing()) if (jstr(m, "added_by") == "research") web.push_back(m);  // yours are never removed
    int removed = 0;
    for (auto it = web.rbegin(); it != web.rend(); ++it) {  // oldest first
        remove(*it);
        ++removed;
        if (sizeMb() <= capMb * 0.9) break;
    }
    return removed;
}

// ── your own builds ─────────────────────────────────────────────
namespace {
Json strList(const Json& in, size_t max = 12) {
    Json out = Json::array();
    if (in.isString()) {
        for (auto& x : split(in.toString(), ',')) if (!trim(x).empty() && out.size() < max) out.push(trim(x));
    } else {
        for (auto& x : in.elements()) if (!trim(x.toString()).empty() && out.size() < max) out.push(truncateUtf8(trim(x.toString()), 80));
    }
    return out;
}
std::string newId() {
    static const char* chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::string t = "my-";
    for (int i = 0; i < 10; ++i) t += chars[rd() % 36];
    return t;
}
}  // namespace

Json MyBuilds::clean(const Json& b) {
    if (!b.isObject()) throw std::runtime_error("a build must be an object");
    std::string game = trim(jstr(b, "game")), weapon = trim(jstr(b, "weapon"));
    if (weapon.empty()) throw std::runtime_error("the build needs a weapon");
    if (gameById(game).id != game || game == "other") throw std::runtime_error("unknown game: " + game);
    Json o = Json::object();
    if (!jstr(b, "id").empty()) o["id"] = truncateUtf8(trim(jstr(b, "id")), 40);
    o["game"] = game;
    o["weapon"] = truncateUtf8(weapon, 60);
    for (const char* k : {"class", "role", "mode", "playstyle", "range", "input", "season"}) o[k] = truncateUtf8(trim(jstr(b, k)), 60);
    Json att = Json::array();
    for (auto& a : b.get("attachments").elements()) {
        if (!a.isArray() || a.size() < 2) continue;
        std::string slot = trim(a.elements()[0].toString()), name = trim(a.elements()[1].toString());
        if (slot.empty() || name.empty() || att.size() >= 10) continue;
        Json p = Json::array();
        p.push(truncateUtf8(slot, 40));
        p.push(truncateUtf8(name, 80));
        att.push(p);
    }
    o["attachments"] = att;
    o["perks"] = strList(b.get("perks"));
    o["equipment"] = strList(b.get("equipment"));
    o["notes"] = truncateUtf8(jstr(b, "notes"), 2000);
    o["created"] = jstr(b, "created");
    o["updated"] = jstr(b, "updated");
    return o;
}

Json MyBuilds::all() const {
    Json data;
    try { data = Json::parse(readFile(file())); } catch (...) { return Json::array(); }
    std::vector<Json> v;
    for (auto& b : data.get("builds").elements()) {
        try { Json c = clean(b); if (!jstr(c, "id").empty()) v.push_back(c); } catch (...) {}
    }
    std::stable_sort(v.begin(), v.end(), [](const Json& a, const Json& b) { return jstr(a, "updated") > jstr(b, "updated"); });
    Json out = Json::array();
    for (auto& b : v) out.push(b);
    return out;
}

Json MyBuilds::forGame(const std::string& gameId) const {
    Json out = Json::array();
    const Json list = all();
    for (auto& b : list.elements()) if (jstr(b, "game") == gameId) out.push(b);
    return out;
}

void MyBuilds::write(const Json& list) const {
    Json data = Json::object();
    data["version"] = 1;
    data["builds"] = list;
    fs::create_directories(file().parent_path());
    writeFileAtomic(file(), data.dump(2));
}

std::string MyBuilds::save(const Json& buildIn) {
    Json b = clean(buildIn);
    const std::string now = utcNowIso();
    if (jstr(b, "id").empty()) b["id"] = newId();
    Json list = all(), out = Json::array();
    bool replaced = false;
    for (auto& x : list.elements()) {
        if (jstr(x, "id") != jstr(b, "id")) { out.push(x); continue; }
        if (jstr(b, "created").empty()) b["created"] = jstr(x, "created");
        replaced = true;
    }
    if (jstr(b, "created").empty()) b["created"] = now;
    b["updated"] = now;
    out.push(b);
    (void)replaced;
    write(out);
    return jstr(b, "id");
}

bool MyBuilds::remove(const std::string& id) {
    Json list = all(), out = Json::array();
    bool found = false;
    for (auto& x : list.elements()) { if (jstr(x, "id") == id) found = true; else out.push(x); }
    if (found) write(out);
    return found;
}

std::string MyBuilds::exportJson() const {
    Json data = Json::object();
    data["kind"] = "cod-redemption-hunter-builds";
    data["version"] = 1;
    data["builds"] = all();
    return data.dump(2);
}

int MyBuilds::importJson(const std::string& text) {
    Json data = Json::parse(text);
    const Json list = data.isArray() ? data : data.get("builds");
    if (!list.isArray()) throw std::runtime_error("no builds in that file");
    int n = 0;
    for (auto& b : list.elements()) {
        try { save(b); ++n; } catch (...) {}
    }
    return n;
}

}  // namespace hunter
