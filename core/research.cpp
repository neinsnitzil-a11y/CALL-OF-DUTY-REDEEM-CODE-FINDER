#include "research.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <regex>
#include <set>
#include <thread>
#include <tuple>

#include "games.hpp"
#include "stats.hpp"
#include "util.hpp"

namespace hunter {

namespace {
const std::string kUserAgent = "COD-Redemption-Hunter/6 (open-source research; respects robots.txt)";

const std::vector<std::string> kSlots = {
    "Optic", "Muzzle", "Barrel", "Underbarrel", "Magazine", "Rear Grip", "Stock", "Laser", "Ammunition", "Fire Mods",
    "Comb", "Bolt", "Apex", "Conversion Kit", "5th slot", "Guard", "Lever", "Trigger Action", "Perk 1", "Perk 2",
    "Perk 3", "Perk 4", "Perks", "Lethal", "Tactical", "Field Upgrade", "Wildcard", "Secondary", "Gear", "Vest",
    "Gloves", "Boots", "Combat Specialty"};
const std::vector<std::pair<std::string, std::string>> kSlotAliases = {
    {"ammo", "Ammunition"}, {"mag", "Magazine"}, {"fire mod", "Fire Mods"}, {"firemods", "Fire Mods"},
    {"rear grip", "Rear Grip"}, {"grip", "Rear Grip"}, {"under barrel", "Underbarrel"}, {"scope", "Optic"},
    {"perk", "Perks"}, {"equipment", "Lethal"}, {"field upgrades", "Field Upgrade"}, {"apex attachment", "Apex"}};
const std::vector<std::string> kSettingNames = {
    "ADS Field of View", "Field of View", "FOV", "World Motion Blur", "Weapon Motion Blur", "Motion Blur", "Film Grain",
    "Depth of Field", "Ray Tracing", "Render Resolution", "Upscaling", "Frame Generation", "NVIDIA Reflex", "V-Sync",
    "VSync", "Frame Rate Limit", "Texture Resolution", "Shadow Quality", "Display Mode", "On-Demand Texture Streaming",
    "Aim Assist Type", "Aim Assist", "Left Stick Min Input Deadzone", "Right Stick Min Input Deadzone",
    "Left Stick Deadzone", "Right Stick Deadzone", "Deadzone", "Horizontal Stick Sensitivity",
    "Vertical Stick Sensitivity", "ADS Sensitivity Multiplier", "ADS Multiplier", "Response Curve Type",
    "Response Curve", "Controller Vibration", "Trigger Effect", "Mouse Sensitivity", "Mouse Acceleration", "DPI",
    "Graphics Quality", "Graphics Preset", "Frame Rate", "Brightness", "Audio Mix", "Gyroscope", "Camera Sensitivity",
    "Firing Sensitivity", "Standard Sensitivity", "ADS Sensitivity", "Tactical Scope Sensitivity",
    "Sniper Scope Sensitivity", "Rotation Speed", "Rotation Mode", "Fire Mode", "Mini Map Opacity", "Real-Time Shadows",
    "Anti-Aliasing", "Bloom", "Ragdoll", "Water Reflection", "Sprint Assist", "Tactical Sprint Behavior", "Auto Sprint",
    "Mantle Assist", "Slide Behavior", "Stick Layout", "Button Layout", "Sensitivity"};
const std::vector<std::pair<std::string, std::string>> kSectionOf = {
    {"graphics", "Graphics"}, {"render", "Graphics"}, {"shadow", "Graphics"}, {"ray", "Graphics"}, {"texture", "Graphics"},
    {"reflex", "Graphics"}, {"sync", "Graphics"}, {"frame", "Graphics"}, {"upscal", "Graphics"}, {"anti-al", "Graphics"},
    {"bloom", "Graphics"}, {"display", "Graphics"}, {"blur", "View"}, {"field of view", "View"}, {"fov", "View"},
    {"grain", "View"}, {"depth", "View"}, {"sensitiv", "Sensitivity"}, {"dpi", "Sensitivity"}, {"gyro", "Sensitivity"},
    {"rotation", "Sensitivity"}, {"deadzone", "Controller"}, {"aim assist", "Controller"}, {"curve", "Controller"},
    {"vibration", "Controller"}, {"trigger", "Controller"}, {"layout", "Controller"}, {"sprint", "Movement"},
    {"mantle", "Movement"}, {"slide", "Movement"}, {"audio", "Audio"}};
const std::map<std::string, std::vector<std::string>> kKindWords = {
    {"builds", {"loadout", "build", "meta", "class", "tier", "gunsmith", "best-gun", "best gun", "attachment", "weapon", "guns"}},
    {"settings", {"setting", "sensitivity", "controller", "graphics", "fov", "keybind", "deadzone", "config", "pc-", "console", "optimi"}},
    {"stats", {"ttk", "stats", "recoil", "damage", "time-to-kill", "weapon-stats"}},
    {"pro", {"pro", "settings", "cdl", "player", "setup"}}};
const std::map<std::string, std::vector<std::string>> kDefaultPages = {
    {"mw4", {"https://lfcarry.com/guides/mw4-best-loadouts", "https://mw4intel.app/best-loadouts",
             "https://warzoneloadout.games/mw4", "https://dving.net/guides/call-of-duty-mw4-best-loadouts-guide",
             "https://wzstats.gg/mw4/loadouts/full-loadouts", "https://codmunity.gg/mw4"}},
    {"warzone", {"https://www.2ndshift.gg/best-warzone-loadouts/", "https://lfcarry.com/guides/warzone-meta",
                 "https://www.sportskeeda.com/call-of-duty-game/best-mw4-warzone-meta-loadouts",
                 "https://dotesports.com/call-of-duty/guides/best-m4-loadout-mw4-warzone",
                 "https://wzstats.gg/best-loadouts/m4-mw4", "https://codmunity.gg/weapon/mw4/m4"}},
    {"bo7", {"https://beebom.com/black-ops-7-best-weapons-loadouts-guide",
             "https://www.esports.net/wiki/guides/black-ops7-weapon-tier-list", "https://codmunity.gg/bo7",
             "https://games.gg/news/best-black-ops-7-season-6-multiplayer-loadouts/"}},
    {"bo6", {"https://wzstats.gg/bo6/meta"}},
    {"codm", {"https://zilliongamer.com/call-of-duty-mobile/c/cod-mobile/best-guns-cod-mobile-2026",
              "https://zilliongamer.com/call-of-duty-mobile/c/weapon-guide/best-switchblade-x9-loadout-cod-mobile",
              "https://zilliongamer.com/call-of-duty-mobile/c/weapon-guide/bal-27-cod-mobile-loadout",
              "https://www.cashify.in/top-best-loadouts-in-cod-mobile-call-of-duty-mobile", "https://www.revibyte.blog/codm/"}}};
const std::map<std::string, std::vector<std::string>> kSettingsPages = {
    {"modern", {"https://playhub.com/blog/call-of-duty/modern-warfare-4-best-settings-125625",
                "https://overgear.com/guides/call-of-duty/modern-warfare-4-best-settings/",
                "https://beebom.com/best-call-of-duty-modern-warfare-4-controller-settings/",
                "https://www.keengamer.com/articles/guides/modern-warfare-4-dmz-best-settings-controller-fps-audio/"}},
    {"codm", {"https://progameguides.com/call-of-duty/best-call-of-duty-mobile-settings/",
              "https://www.charlieintel.com/call-of-duty-mobile/best-cod-mobile-settings-multiplayer-145921/",
              "https://www.esports.net/wiki/guides/best-cod-mobile-settings"}}};

const char* kBuildSystem = "You extract Call of Duty weapon builds from web pages. Use ONLY what the page says — never "
                           "invent weapons or attachments. If the page has no builds, return an empty list.";
const char* kBuildPrompt = R"(Game: %GAME%
Page: %URL%

Extract every weapon build (loadout / class setup) on this page as JSON:
{"builds": [{"weapon": "M4", "class": "Assault rifle", "role": "long range", "mode": "warzone|multiplayer|ranked|any",
  "attachments": [["Optic", "Locus PX3 Canted"], ["Muzzle", "FSS IS300"]], "perks": ["..."], "equipment": ["..."]}]}

Page text:
%TEXT%)";
const char* kImagePrompt = R"(This image is from a %GAME% build guide (%URL%).
If it shows a weapon loadout / gunsmith screen, read it as JSON:
{"weapon": "...", "attachments": [["Slot", "Attachment name"]], "perks": [], "equipment": []}
If it is not a loadout screenshot, return {"none": true}. Only write what you can actually read.)";
const char* kSettingsPrompt = R"(Game: %GAME%
Page: %URL%

List every recommended in-game setting on this page as JSON:
{"settings": [{"section": "Controller|Graphics|View|Audio|Sensitivity|...", "setting": "Field of view",
  "value": "110", "why": "short reason from the page"}]}
Use only what the page says.

Page text:
%TEXT%)";

std::string fill(std::string t, const std::string& game, const std::string& url, const std::string& text = "") {
    t = replaceAll(t, "%GAME%", game);
    t = replaceAll(t, "%URL%", url);
    return replaceAll(t, "%TEXT%", text);
}

std::string escapeRx(const std::string& s) {
    static const std::string special = R"(\^$.|?*+()[]{}/-)";
    std::string out;
    for (char c : s) {
        if (special.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}
const std::string kBullet = "(?:-|\xE2\x80\xA2|\\*|\xC2\xB7|\xE2\x96\xAA|\xE2\x9E\xA4|>)";  // - • * · ▪ ➤ >
const std::string kDash = "(?::|-|\xE2\x80\x93|\xE2\x80\x94|\\|)";                          // : - – — |

const std::regex& slotRe() {
    static const std::regex r = [] {
        std::set<std::string> names;
        for (auto& s : kSlots) names.insert(lower(s));
        for (auto& [a, _] : kSlotAliases) names.insert(a);
        std::vector<std::string> v(names.begin(), names.end());
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.size() > b.size(); });
        std::vector<std::string> esc;
        for (auto& n : v) esc.push_back(escapeRx(n));
        return re("^(?:" + kBullet + "\\s*)?(" + join(esc, "|") + ")\\s*(?:" + kDash + "\\s*|\\s+)(.{2,60})$", true);
    }();
    return r;
}
const std::regex& settingRe() {
    static const std::regex r = [] {
        std::vector<std::string> esc;
        for (auto& n : kSettingNames) esc.push_back(escapeRx(n));
        return re("^(?:" + kBullet + "\\s*)?(" + join(esc, "|") + ")\\b\\s*(?:\\([^)]{0,20}\\))?\\s*(?:" + kDash.substr(0, kDash.size() - 1) +
                      "|=)\\s*|\\s+)(.{1,40})$",
                  true);
    }();
    return r;
}
const std::regex& weaponHeadRe() {
    static const std::regex r = re(R"(\b(?:best|meta|top)\s+(.{2,30}?)\s+(?:loadout|build|class(?: setup)?|attachments|setup))", true);
    return r;
}

std::string stripChars(std::string s, const std::string& chars) {
    size_t a = s.find_first_not_of(chars), b = s.find_last_not_of(chars);
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::string titleCase(const std::string& s) {
    std::string out;
    bool start = true;
    for (char c : s) {
        if (std::isalpha(static_cast<unsigned char>(c))) {
            out += start ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                         : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            start = false;
        } else {
            out += c;
            start = true;
        }
    }
    return out;
}

// Counter that remembers first-seen order (Python's Counter.most_common keeps it for ties).
struct OrderedCounter {
    std::vector<std::pair<std::string, int>> items;
    void add(const std::string& k, int n = 1) {
        for (auto& [key, c] : items) if (key == k) { c += n; return; }
        items.emplace_back(k, n);
    }
    std::vector<std::pair<std::string, int>> mostCommon() const {
        auto v = items;
        std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second > b.second; });
        return v;
    }
};

Json strList(const std::vector<std::string>& v) {
    Json a = Json::array();
    for (auto& s : v) a.push(s);
    return a;
}

Json cleanBuild(const Json& b) {
    Json att = Json::array();
    for (const auto& a : b.get("attachments").elements()) {
        std::string slot, name;
        if (a.isArray() && a.size() >= 2) { slot = a.elements()[0].toString(); name = a.elements()[1].toString(); }
        else if (a.isObject()) { slot = jstr(a, "slot"); name = jstr(a, "name"); }
        if (!slot.empty() && !name.empty() && att.size() < 12) {
            Json pair = Json::array();
            pair.push(canonSlot(slot));
            pair.push(truncateUtf8(trim(name), 60));
            att.push(pair);
        }
    }
    auto list = [&](const char* k) {
        Json out = Json::array();
        for (const auto& p : b.get(k).elements())
            if (!p.toString().empty() && out.size() < 6) out.push(truncateUtf8(p.toString(), 40));
        return out;
    };
    Json o = Json::object();
    o["weapon"] = truncateUtf8(trim(jstr(b, "weapon")), 40);
    o["attachments"] = att;
    o["class"] = truncateUtf8(jstr(b, "class"), 30);
    o["role"] = truncateUtf8(jstr(b, "role"), 60);
    o["perks"] = list("perks");
    o["equipment"] = list("equipment");
    return o;
}
}  // namespace

bool containsWord(const std::string& text, const std::string& word) {
    if (word.empty()) return false;
    std::string t = lower(text), w = lower(word);
    for (size_t p = t.find(w); p != std::string::npos; p = t.find(w, p + 1)) {
        bool before = p == 0 || !isAsciiAlnum(t[p - 1]);
        bool after = p + w.size() >= t.size() || !isAsciiAlnum(t[p + w.size()]);
        if (before && after) return true;
    }
    return false;
}

std::string canonSlot(const std::string& s) {
    std::string low = lower(trim(s));
    for (const auto& slot : kSlots) if (lower(slot) == low) return slot;
    for (const auto& [a, c] : kSlotAliases) if (a == low) return c;
    return titleCase(trim(s));
}

Json buildsFromDocument(const Document& doc, const std::vector<std::string>& weapons) {
    auto weaponIn = [&](const std::string& text) -> std::string {
        std::smatch m;
        bool head = std::regex_search(text, m, weaponHeadRe());
        for (const auto& w : weapons) if (containsWord(text, w)) return w;
        return head ? stripChars(m[1].str(), " :-") : "";
    };
    Json builds = Json::array();
    std::string current;
    std::vector<std::pair<std::string, std::string>> attach;
    std::optional<std::string> headingSeen;
    auto flush = [&] {
        if (current.empty() || attach.size() < 3) return;
        Json att = Json::array();
        std::set<std::pair<std::string, std::string>> seen;
        for (auto& a : attach)
            if (seen.insert(a).second) {
                Json p = Json::array();
                p.push(a.first);
                p.push(a.second);
                att.push(p);
            }
        Json b = Json::object();
        b["weapon"] = current;
        b["attachments"] = att;
        b["via"] = "text";
        builds.push(b);
    };
    for (const auto& b : doc.blocks) {
        if (!headingSeen || *headingSeen != b.heading) {
            headingSeen = b.heading;
            std::string w = weaponIn(b.heading);
            if (!w.empty() && w != current) { flush(); current = w; attach.clear(); }
        }
        std::string text = trim(b.text);
        std::smatch m;
        if (std::regex_match(text, m, slotRe())) {
            std::string slot = canonSlot(m[1].str()), name = stripChars(trim(m[2].str()), " .;,");
            bool repeat = std::any_of(attach.begin(), attach.end(), [&](auto& a) { return a.first == slot; });
            if (repeat && slot != "Perks") { flush(); attach.clear(); }  // a second "Optic" = the next build
            attach.emplace_back(slot, name);
        } else if (b.text.size() < 80) {
            std::string w = weaponIn(b.text);
            if (!w.empty() && w != current && (search(b.text, weaponHeadRe()) || lower(trim(b.text)) == lower(w))) {
                flush();
                current = w;
                attach.clear();
            }
        }
    }
    flush();
    return builds;
}

Json settingsFromDocument(const Document& doc) {
    Json out = Json::array();
    static const std::set<std::string> fillers = {"the", "a", "an", "is", "and", "to"};
    for (const auto& b : doc.blocks) {
        std::string text = trim(b.text);
        std::smatch m;
        if (!std::regex_match(text, m, settingRe())) continue;
        std::string name = trim(m[1].str()), value = stripChars(trim(m[2].str()), " .;,");
        size_t words = 0;
        for (auto& w : split(collapseSpaces(value), ' ')) if (!w.empty()) ++words;
        if (words > 6 || fillers.count(lower(value))) continue;  // a sentence, not a value
        std::string low = lower(name), section;
        for (const auto& [k, sec] : kSectionOf) if (low.find(k) != std::string::npos) { section = sec; break; }
        std::string heading = trim(b.heading);
        if (section.empty()) section = !heading.empty() && heading.size() < 40 ? truncateUtf8(heading, 30) : "General";
        Json s = Json::object();
        s["section"] = section;
        s["setting"] = name;
        s["value"] = value;
        s["why"] = "";
        s["via"] = "text";
        out.push(s);
    }
    return out;
}

std::vector<std::string> findLinks(const std::string& html, const std::string& baseUrl, const std::string& gameId,
                                   const std::string& kind, size_t limit, bool gameSpecific) {
    const Game& g = gameById(gameId);
    std::set<std::string> base;
    for (auto& a : g.aliases) base.insert(lower(a));
    base.insert(lower(g.name));
    base.insert(lower(g.shortName.empty() ? g.name : g.shortName));
    base.insert(lower(g.id));
    std::set<std::string> words;
    for (auto& w : base) {
        words.insert(w);
        words.insert(replaceAll(w, " ", "-"));
        words.insert(replaceAll(w, " ", ""));
    }
    for (auto it = words.begin(); it != words.end();) it = it->size() < 3 ? words.erase(it) : std::next(it);
    if (gameId == "codm") words.insert({"cod-mobile", "codm", "call-of-duty-mobile", "codmobile"});
    auto kw = kKindWords.count(kind) ? kKindWords.at(kind) : kKindWords.at("builds");
    auto lastTwo = [](const std::string& host) {
        auto parts = split(host, '.');
        return parts.size() >= 2 ? parts[parts.size() - 2] + "." + parts.back() : host;
    };
    std::string baseDom = lastTwo(parseUrl(baseUrl).host);
    static const std::regex skip = re(R"(login|signin|account|privacy|terms|/tag/|/author/)", true);
    std::vector<std::pair<std::string, int>> scored;  // insertion order
    std::string baseTrim = baseUrl;
    while (!baseTrim.empty() && baseTrim.back() == '/') baseTrim.pop_back();
    for (const auto& link : extractLinks(html, baseUrl)) {
        const std::string& url = link.url;
        if (!startsWith(url, "https://") || lastTwo(parseUrl(url).host) != baseDom) continue;
        std::string t = url;
        while (!t.empty() && t.back() == '/') t.pop_back();
        if (t == baseTrim || search(url, skip)) continue;
        std::string text = lower(parseUrl(url).path + " " + link.text);
        bool gameHit = std::any_of(words.begin(), words.end(), [&](auto& w) { return text.find(w) != std::string::npos; });
        int kindHit = 0;
        for (auto& w : kw) if (text.find(w) != std::string::npos) ++kindHit;
        if ((gameHit || gameSpecific) && kindHit) {
            int score = kindHit + (gameHit ? 2 : 0);
            auto it = std::find_if(scored.begin(), scored.end(), [&](auto& s) { return s.first == url; });
            if (it == scored.end()) scored.emplace_back(url, score);
            else it->second = std::max(it->second, score);
        }
    }
    std::stable_sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.second > b.second; });
    std::vector<std::string> out;
    for (auto& [u, s] : scored) if (out.size() < limit) out.push_back(u);
    return out;
}

std::vector<std::pair<std::string, std::string>> pageImages(const std::string& html, const std::string& baseUrl, size_t limit,
                                                            bool settingsImages) {
    static const std::regex buildHint = re("loadout|build|attachment|gunsmith|class|meta|setup|weapon", true);
    static const std::regex settingsHint = re("setting|sensitiv|graphic|controller|deadzone|fov|layout|hud|keybind|config", true);
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& img : extractImages(html)) {
        std::string file = img.src.substr(img.src.rfind('/') == std::string::npos ? 0 : img.src.rfind('/') + 1);
        std::string hint = img.alt + " " + img.title + " " + file;
        if (img.src.empty() || startsWith(img.src, "data:") || !search(hint, settingsImages ? settingsHint : buildHint)) continue;
        std::string url = resolveUrl(baseUrl, img.src);
        if (startsWith(url, "https://") &&
            std::none_of(out.begin(), out.end(), [&](auto& o) { return o.first == url; }))
            out.emplace_back(url, truncateUtf8(trim(img.alt), 120));
        if (out.size() >= limit) break;
    }
    return out;
}

Json crossCheck(const Json& found, const Json& curated) {
    struct Group {
        std::vector<const Json*> items;
        OrderedCounter names;
    };
    std::vector<std::pair<std::string, Group>> byWeapon;
    for (const auto& f : found.elements()) {
        std::string key = normKey(jstr(f, "weapon"));
        if (key.size() < 2) continue;
        auto it = std::find_if(byWeapon.begin(), byWeapon.end(), [&](auto& p) { return p.first == key; });
        if (it == byWeapon.end()) { byWeapon.emplace_back(key, Group{}); it = std::prev(byWeapon.end()); }
        it->second.items.push_back(&f);
        it->second.names.add(trim(jstr(f, "weapon")));
    }
    std::map<std::string, const Json*> curatedBy;
    for (const auto& b : curated.elements()) curatedBy[normKey(jstr(b, "weapon"))] = &b;
    std::vector<Json> rows;
    for (auto& [key, grp] : byWeapon) {
        std::set<std::string> domains;
        for (auto* i : grp.items) domains.insert(urlDomain(jstr(*i, "url")));
        std::vector<std::pair<std::string, OrderedCounter>> slots;  // slot → counter of normalised names
        std::map<std::pair<std::string, std::string>, std::string> shown;
        for (auto* i : grp.items)
            for (const auto& a : i->get("attachments").elements()) {
                if (a.size() < 2) continue;
                std::string s = canonSlot(a.elements()[0].toString()), n = a.elements()[1].toString();
                auto it = std::find_if(slots.begin(), slots.end(), [&](auto& p) { return p.first == s; });
                if (it == slots.end()) { slots.emplace_back(s, OrderedCounter{}); it = std::prev(slots.end()); }
                it->second.add(normKey(n));
                shown.emplace(std::make_pair(s, normKey(n)), trim(n));
            }
        std::vector<std::string> order;
        for (auto& s : kSlots)
            if (std::any_of(slots.begin(), slots.end(), [&](auto& p) { return p.first == s; })) order.push_back(s);
        for (auto& [s, _] : slots)
            if (std::find(kSlots.begin(), kSlots.end(), s) == kSlots.end()) order.push_back(s);
        Json agreed = Json::array();
        int voteSum = 0;
        for (auto& s : order) {
            auto& counter = std::find_if(slots.begin(), slots.end(), [&](auto& p) { return p.first == s; })->second;
            auto best = counter.mostCommon().front();
            if (agreed.size() < 12) {
                Json a = Json::array();
                a.push(s);
                a.push(shown[{s, best.first}]);
                a.push(best.second);
                agreed.push(a);
                voteSum += best.second;
            }
        }
        Json match;
        auto cur = curatedBy.find(key);
        if (cur != curatedBy.end() && cur->second->get("attachments").size()) {
            std::set<std::pair<std::string, std::string>> curSet;
            for (const auto& a : cur->second->get("attachments").elements())
                curSet.insert({canonSlot(a.elements()[0].toString()), normKey(a.elements()[1].toString())});
            int hits = 0;
            for (const auto& a : agreed.elements())
                if (curSet.count({a.elements()[0].toString(), normKey(a.elements()[1].toString())})) ++hits;
            match = std::to_string(hits) + "/" + std::to_string(cur->second->get("attachments").size()) +
                    " attachments match the curated build";
        }
        auto top = [&](const char* k) {
            OrderedCounter c;
            for (auto* i : grp.items)
                for (const auto& x : i->get(k).elements())
                    if (x.isString() && !trim(x.toString()).empty()) c.add(trim(x.toString()));
            std::vector<std::string> v;
            for (auto& [n, _] : c.mostCommon()) if (v.size() < 4) v.push_back(n);
            return strList(v);
        };
        auto first = [&](const char* k) {
            for (auto* i : grp.items) if (!jstr(*i, k).empty()) return jstr(*i, k);
            return std::string();
        };
        std::set<std::string> via;
        Json sources = Json::array();
        for (auto* i : grp.items) {
            via.insert(jstr(*i, "via", "text"));
            if (sources.size() < 8) {
                Json s = Json::object();
                s["url"] = jstr(*i, "url");
                s["via"] = jstr(*i, "via", "text");
                s["image"] = jstr(*i, "image");
                s["file"] = jstr(*i, "file");
                sources.push(s);
            }
        }
        Json row = Json::object();
        row["weapon"] = grp.names.mostCommon().front().first;
        row["sites"] = static_cast<int>(domains.size());
        row["domains"] = strList(std::vector<std::string>(domains.begin(), domains.end()));
        row["verified"] = domains.size() >= 2;
        row["attachments"] = agreed;
        row["perks"] = top("perks");
        row["equipment"] = top("equipment");
        row["class"] = first("class");
        row["role"] = first("role");
        row["via"] = strList(std::vector<std::string>(via.begin(), via.end()));
        row["sources"] = sources;
        row["curated_match"] = match;
        row["_votes"] = voteSum;
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) {
        auto ka = std::make_tuple(-jinteger(a, "sites"), -jinteger(a, "_votes"), jstr(a, "weapon"));
        auto kb = std::make_tuple(-jinteger(b, "sites"), -jinteger(b, "_votes"), jstr(b, "weapon"));
        return ka < kb;
    });
    Json out = Json::array();
    for (auto& r : rows) { r.erase("_votes"); out.push(r); }
    return out;
}

Json mergeSettings(const Json& found) {
    std::vector<std::pair<std::string, std::vector<const Json*>>> groups;
    for (const auto& f : found.elements()) {
        if (jstr(f, "setting").empty() || f.get("value").toString().empty()) continue;
        std::string k = normKey(jstr(f, "setting"));
        auto it = std::find_if(groups.begin(), groups.end(), [&](auto& g) { return g.first == k; });
        if (it == groups.end()) { groups.emplace_back(k, std::vector<const Json*>{}); it = std::prev(groups.end()); }
        it->second.push_back(&f);
    }
    std::vector<Json> rows;
    for (auto& [k, items] : groups) {
        OrderedCounter values;
        std::set<std::string> domains, srcs;
        for (auto* i : items) {
            values.add(trim(i->get("value").toString()));
            domains.insert(urlDomain(jstr(*i, "url")));
            srcs.insert(jstr(*i, "url"));
        }
        auto mc = values.mostCommon();
        Json row = Json::object();
        row["section"] = jstr(*items[0], "section", "General").empty() ? "General" : jstr(*items[0], "section", "General");
        row["setting"] = trim(jstr(*items[0], "setting"));
        row["value"] = mc[0].first;
        row["votes"] = mc[0].second;
        row["sites"] = static_cast<int>(domains.size());
        std::string why;
        for (auto* i : items) if (!jstr(*i, "why").empty()) { why = jstr(*i, "why"); break; }
        row["why"] = why;
        std::vector<std::string> s(srcs.begin(), srcs.end());
        if (s.size() > 5) s.resize(5);
        row["sources"] = strList(s);
        std::vector<std::string> other;
        for (size_t x = 1; x < mc.size() && x < 4; ++x) other.push_back(mc[x].first);
        row["other_values"] = strList(other);
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) {
        return std::make_tuple(jstr(a, "section"), -jinteger(a, "sites"), jstr(a, "setting")) <
               std::make_tuple(jstr(b, "section"), -jinteger(b, "sites"), jstr(b, "setting"));
    });
    Json out = Json::array();
    for (auto& r : rows) out.push(r);
    return out;
}

std::string docText(const Document& doc, size_t limit) {
    std::vector<std::string> out;
    std::optional<std::string> heading;
    size_t size = 0;
    for (const auto& b : doc.blocks) {
        if (!heading || *heading != b.heading) {
            heading = b.heading;
            out.push_back("\n## " + b.heading);
        }
        out.push_back(b.text);
        size += b.text.size();
        if (size > limit) break;
    }
    return truncateUtf8(join(out, "\n"), limit);
}

std::string researchText(const Json& row, const std::string& gameLabel) {
    std::vector<std::string> lines;
    lines.push_back(jstr(row, "weapon") + (gameLabel.empty() ? "" : " — " + gameLabel));
    std::vector<std::string> doms;
    for (auto& d : row.get("domains").elements()) doms.push_back(d.toString());
    lines.push_back("Seen on " + std::to_string(jinteger(row, "sites")) + " site(s): " + join(doms, ", ") +
                    (jbool(row, "verified") ? " ✔ verified" : ""));
    for (const auto& a : row.get("attachments").elements()) {
        long long votes = a.size() > 2 ? a.elements()[2].toInt() : 1;
        lines.push_back("  " + a.elements()[0].toString() + ": " + a.elements()[1].toString() +
                        (votes > 1 ? "  (" + std::to_string(votes) + " sites agree)" : ""));
    }
    auto list = [](const Json& arr) {
        std::vector<std::string> v;
        for (auto& x : arr.elements()) v.push_back(x.toString());
        return join(v, ", ");
    };
    if (row.get("perks").size()) lines.push_back("Perks: " + list(row.get("perks")));
    if (row.get("equipment").size()) lines.push_back("Equipment: " + list(row.get("equipment")));
    if (!jstr(row, "curated_match").empty()) lines.push_back("Check: " + jstr(row, "curated_match"));
    int n = 0;
    for (const auto& s : row.get("sources").elements())
        if (n++ < 4) lines.push_back("Source: " + jstr(s, "url") + (jstr(s, "via") == "image" ? " (read from a screenshot)" : ""));
    return join(lines, "\n");
}

// ── results file ────────────────────────────────────────────────
Json ResearchStore::load() const {
    try { return Json::parse(readFile(path_)); } catch (...) { return Json::parse(R"({"games":{}})"); }
}
Json ResearchStore::forGame(const std::string& gameId) const { return load().get("games").get(gameId); }
void ResearchStore::save(const std::string& gameId, const std::string& part, const Json& result) {
    Json d = load();
    d["games"][gameId][part] = result;
    d["updated"] = utcNowIso();
    writeFileAtomic(path_, d.dump(1));
}

// ── researcher ──────────────────────────────────────────────────
Researcher::Researcher(const Json& cfg, HttpClient& http, const MetaData& meta, const SourceCatalog& catalog,
                       ScreenshotLibrary& shots, AiClient* ai, std::function<void(const std::string&)> onLine)
    : cfg_(cfg), rcfg_(cfg.get("research")), http_(http), meta_(meta), catalog_(catalog), shots_(shots),
      ai_(ai && ai->ready() ? ai : nullptr), onLine_(std::move(onLine)) {
    imageBudget_ = static_cast<int>(jinteger(rcfg_, "max_images_per_run", 12));
    renderBudget_ = static_cast<int>(jinteger(rcfg_, "max_renders_per_run", 3));
    sleeper = [](double s) { std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(s * 1000))); };
}

std::vector<std::string> Researcher::pagesFor(const std::string& gameId, const std::string& kind) const {
    std::vector<std::string> out;
    auto add = [&](const std::string& u) { if (!u.empty() && std::find(out.begin(), out.end(), u) == out.end()) out.push_back(u); };
    if (kind == "stats" || kind == "patches") {
        for (auto& u : rcfg_.get(kind + "_pages").get(gameId).elements()) add(u.toString());
        return out;
    }
    Json game = meta_.forGame(gameId);
    if (kind == "settings") {
        for (auto& u : rcfg_.get("settings_pages").get(gameId).elements()) add(u.toString());
        for (auto& u : kSettingsPages.at(gameId == "codm" ? "codm" : "modern")) add(u);
        for (auto& [k, v] : game.get("settings").get("sources").items()) add(v.toString());
        return out;
    }
    for (auto& u : rcfg_.get("pages").get(gameId).elements()) add(u.toString());
    if (kDefaultPages.count(gameId)) for (auto& u : kDefaultPages.at(gameId)) add(u);
    for (auto& l : game.get("live").elements())
        if (l.isArray() && l.size() >= 2) add(l.elements()[1].toString());
    return out;
}

std::vector<Researcher::PlanEntry> Researcher::plan(const std::string& gameId, const std::string& kind) const {
    std::vector<PlanEntry> entries;
    for (auto& u : pagesFor(gameId, kind)) entries.push_back({urlDomain(u), u, "", false, true});
    if (rcfg_.get("use_catalog").toBool(true))
        for (auto& c : catalog_.forGame(cfg_, gameId, kind)) entries.push_back({c.name, c.url, c.note, c.crawl, c.gameSpecific});
    std::vector<PlanEntry> out;
    std::set<std::string> seen;
    for (auto& e : entries) if (seen.insert(e.url).second) out.push_back(e);
    return out;
}

bool Researcher::allowed(const std::string& url) {
    if (!rcfg_.get("respect_robots_txt").toBool(true)) return true;
    Url u = parseUrl(url);
    std::string base = u.scheme + "://" + u.host;
    auto it = robots_.find(base);
    if (it == robots_.end()) {
        HttpResponse r = http_.get(base + "/robots.txt", {{"User-Agent", kUserAgent}}, 15);
        it = robots_.emplace(base, Robots(r.status == 200 ? r.body : "", kUserAgent)).first;
    }
    return it->second.allows(url);
}

std::pair<std::string, std::string> Researcher::fetch(const std::string& url) {
    if (!startsWith(url, "https://")) throw std::runtime_error("HTTPS only");
    if (!allowed(url)) throw std::runtime_error("the site's robots.txt asks tools not to read this page — skipped");
    HttpResponse r = http_.get(url, {{"User-Agent", kUserAgent}}, 25);
    if (r.status == 403 || r.status == 429) throw std::runtime_error("the site answered HTTP " + std::to_string(r.status) + " — not retrying");
    if (r.status == 0) throw std::runtime_error(r.error.empty() ? "network error" : r.error);
    if (r.status >= 400) throw std::runtime_error("HTTP " + std::to_string(r.status));
    std::string finalUrl = r.finalUrl.empty() ? url : r.finalUrl;
    std::string html = r.body;
    std::string visible;
    for (auto& b : htmlToDocument(html, finalUrl).blocks) visible += b.text + " ";
    if (looksJsRendered(html, visible) && renderBudget_ > 0 && renderer) {
        --renderBudget_;
        try { html = renderer(finalUrl); } catch (...) {}
    }
    return {html, finalUrl};
}

std::optional<AiImage> Researcher::image(const std::string& url) {
    if (imageBudget_ <= 0 || !startsWith(url, "https://") || !allowed(url)) return std::nullopt;
    HttpResponse r = http_.get(url, {{"User-Agent", kUserAgent}}, 20);
    std::string ctype = trim(r.header("content-type").substr(0, r.header("content-type").find(';')));
    static const std::set<std::string> ok = {"image/png", "image/jpeg", "image/webp", "image/gif"};
    if (r.status != 200 || !ok.count(ctype)) return std::nullopt;
    if (r.body.size() > 3000000 || r.body.size() < 5000) return std::nullopt;  // too big to send, or an icon
    --imageBudget_;
    return AiImage{ctype, r.body};
}

Json Researcher::walk(const std::string& gameId, const std::string& kind,
                      const std::function<std::string(const std::string&, const std::string&, const PlanEntry&)>& handle,
                      int maxPages) {
    int limit = maxPages ? maxPages : static_cast<int>(jinteger(rcfg_, "max_pages", 14));
    std::vector<PlanEntry> queue = plan(gameId, kind);
    Json report = Json::array();
    std::set<std::string> done;
    int fetched = 0;
    size_t linksPerHub = static_cast<size_t>(jinteger(rcfg_, "crawl_links_per_site", 3));
    while (!queue.empty() && fetched < limit) {
        if (stop && stop->load()) break;
        PlanEntry e = queue.front();
        queue.erase(queue.begin());
        if (!done.insert(e.url).second) continue;
        if (fetched && sleeper) sleeper(jnum(rcfg_, "delay_seconds", 1.5));
        line("Reading " + (e.name.empty() ? urlDomain(e.url) : e.name) + " …");
        std::string html, finalUrl;
        try {
            std::tie(html, finalUrl) = fetch(e.url);
        } catch (const std::exception& ex) {
            std::string msg = ex.what();
            std::string path = parseUrl(e.url).path;
            if (e.crawl && msg.find("404") != std::string::npos && path != "/" && !path.empty()) {
                PlanEntry root = e;  // the section moved: start again from the site's home page
                root.url = "https://" + parseUrl(e.url).host + "/";
                root.gameSpecific = false;
                queue.insert(queue.begin(), root);
            }
            Json r = Json::object();
            r["url"] = e.url;
            r["ok"] = false;
            r["note"] = truncateUtf8(e.note.empty() ? msg : e.note, 160);
            report.push(r);
            line("  skipped: " + truncateUtf8(e.note.empty() ? msg : e.note, 110));
            continue;
        }
        ++fetched;
        std::string note = handle(html, finalUrl, e);
        Json r = Json::object();
        r["url"] = finalUrl;
        r["ok"] = true;
        r["note"] = note;
        report.push(r);
        if (!note.empty()) line("  " + note);
        if (e.crawl) {  // followed pages are read next, before the remaining hubs
            std::vector<PlanEntry> follow;
            for (auto& u : findLinks(html, finalUrl, gameId, kind, linksPerHub, e.gameSpecific))
                if (!done.count(u)) follow.push_back({urlDomain(u) + " → " + parseUrl(u).path.substr(0, 40), u, "", false, true});
            queue.insert(queue.begin(), follow.begin(), follow.end());
        }
    }
    return report;
}

Json Researcher::aiBuilds(const std::string& label, const std::string& url, const Document& doc) {
    std::string text = docText(doc, 14000);
    Json out = Json::array();
    if (text.size() < 200) return out;
    try {
        Json data = ai_->chatJson(fill(kBuildPrompt, label, url, text), kBuildSystem);
        for (const auto& b : data.get("builds").elements()) {
            if (!b.isObject() || jstr(b, "weapon").empty()) continue;
            Json c = cleanBuild(b);
            c["url"] = url;
            c["via"] = "ai";
            out.push(c);
        }
    } catch (const std::exception& e) {
        line(std::string("  AI: ") + e.what());
    }
    return out;
}

Json Researcher::imageBuilds(const std::string& gameId, const std::string& label, const std::string& url,
                             const std::string& html, const std::vector<std::string>& weapons) {
    Json out = Json::array();
    bool aiOk = ai_ != nullptr;
    bool save = rcfg_.get("save_screenshots").toBool(true);
    for (auto& [imgUrl, alt] : pageImages(html, url, static_cast<size_t>(jinteger(rcfg_, "max_images_per_page", 3)))) {
        if (stop && stop->load()) break;
        auto img = image(imgUrl);
        if (!img) continue;
        std::string label2 = alt + " " + replaceAll(imgUrl.substr(imgUrl.rfind('/') + 1), "-", " ");
        std::string guess;
        for (auto& w : weapons) if (containsWord(label2, w)) { guess = w; break; }
        Json data;
        if (aiOk) {
            try {
                data = ai_->chatJson(fill(kImagePrompt, label, url), kBuildSystem, {*img});
            } catch (const std::exception& e) {
                line(std::string("  AI image read failed: ") + e.what());
                aiOk = false;  // usually: this model can't read images — keep saving, stop asking
            }
        }
        if (data.isObject() && jbool(data, "none")) continue;  // the AI says it isn't a loadout screenshot
        bool read = data.isObject() && !jstr(data, "weapon").empty();
        Json meta;
        if (save) {
            try {
                meta = shots_.save(gameId, img->bytes, img->mime, read ? jstr(data, "weapon") : guess, url, imgUrl, alt,
                                   "research", "", jnum(rcfg_, "screenshot_library_mb", 500));
                line("  saved screenshot" + (jstr(meta, "weapon").empty() ? "" : ": " + jstr(meta, "weapon")));
            } catch (const std::exception& e) {
                line(std::string("  couldn't save screenshot: ") + e.what());
            }
        }
        if (read) {
            Json c = cleanBuild(data);
            c["url"] = url;
            c["via"] = "image";
            c["image"] = imgUrl;
            c["file"] = jstr(meta, "file");
            out.push(c);
            line("  read a build screenshot: " + jstr(data, "weapon"));
        }
    }
    return out;
}

Json Researcher::researchBuilds(const std::string& gameId, bool useImages) {
    std::string label = gameById(gameId).name;
    Json found = Json::array();
    auto weapons = meta_.knownWeapons();
    Json pages = walk(gameId, "builds", [&](const std::string& html, const std::string& finalUrl, const PlanEntry&) {
        Document doc = htmlToDocument(html, finalUrl);
        Json got = buildsFromDocument(doc, weapons);
        for (auto& b : got.elements()) b["url"] = finalUrl;
        if (ai_) { const Json more = aiBuilds(label, finalUrl, doc); for (auto& b : more.elements()) got.push(b); }
        if (useImages) { const Json more = imageBuilds(gameId, label, finalUrl, html, weapons); for (auto& b : more.elements()) got.push(b); }
        for (auto& b : got.elements()) found.push(b);
        return std::to_string(got.size()) + " build(s)";
    });
    Json rows = crossCheck(found, meta_.forGame(gameId).get("builds"));
    Json result = Json::object();
    result["updated"] = utcNowIso();
    result["weapons"] = rows;
    result["pages"] = pages;
    result["ai"] = ai_ != nullptr;
    ResearchStore(Paths{{}, shots_.root().parent_path()}).save(gameId, "builds", result);
    int verified = 0, ok = 0;
    for (auto& r : rows.elements()) verified += jbool(r, "verified");
    for (auto& p : pages.elements()) ok += jbool(p, "ok");
    line("Done: " + std::to_string(rows.size()) + " weapon(s), " + std::to_string(verified) + " confirmed by 2+ sites, " +
         std::to_string(ok) + " page(s) read.");
    return result;
}

Json Researcher::researchSettings(const std::string& gameId, bool useImages) {
    std::string label = gameById(gameId).name;
    Json found = Json::array();
    Json pages = walk(gameId, "settings", [&](const std::string& html, const std::string& finalUrl, const PlanEntry&) {
        Document doc = htmlToDocument(html, finalUrl);
        Json items = settingsFromDocument(doc);
        for (auto& x : items.elements()) x["url"] = finalUrl;
        if (ai_) {
            try {
                Json data = ai_->chatJson(fill(kSettingsPrompt, label, finalUrl, docText(doc, 14000)));
                for (const auto& x : data.get("settings").elements()) {
                    if (!x.isObject()) continue;
                    Json y = x;
                    y["url"] = finalUrl;
                    y["via"] = "ai";
                    items.push(y);
                }
            } catch (const std::exception& e) {
                line(std::string("  AI: ") + e.what());
            }
        }
        if (useImages) saveImages(gameId, finalUrl, html, true);
        for (auto& x : items.elements()) found.push(x);
        return std::to_string(items.size()) + " setting(s)";
    }, static_cast<int>(jinteger(rcfg_, "max_settings_pages", 8)));
    Json result = Json::object();
    result["updated"] = utcNowIso();
    result["settings"] = mergeSettings(found);
    result["pages"] = pages;
    result["ai"] = ai_ != nullptr;
    ResearchStore(Paths{{}, shots_.root().parent_path()}).save(gameId, "settings", result);
    int ok = 0;
    for (auto& p : pages.elements()) ok += jbool(p, "ok");
    line("Done: " + std::to_string(result.get("settings").size()) + " setting(s) from " + std::to_string(ok) + " page(s).");
    return result;
}

Json Researcher::researchStats(const std::string& gameId, Store& store) {
    std::string label = gameById(gameId).name;
    auto weapons = meta_.knownWeapons();
    std::set<std::string> seen;
    Json changes = Json::array();
    Json pages = walk(gameId, "stats", [&](const std::string& html, const std::string& finalUrl, const PlanEntry&) {
        Document doc = htmlToDocument(html, finalUrl);
        Json recs = statsFromDocument(doc, weapons);
        if (ai_) {
            try {
                Json data = ai_->chatJson(fill(kStatsPrompt, label, finalUrl, docText(doc, 14000)));
                for (const auto& r : data.get("weapons").elements())
                    if (r.isObject() && !jstr(r, "weapon").empty()) recs.push(r);
            } catch (const std::exception& e) {
                line(std::string("  AI: ") + e.what());
            }
        }
        int n = 0;
        for (const auto& rec : recs.elements()) {
            Json ch = recordStats(store, gameId, rec, urlDomain(finalUrl));
            for (const auto& c : ch.elements()) {
                Json x = Json::array();
                x.push(jstr(rec, "weapon"));
                for (const auto& v : c.elements()) x.push(v);
                changes.push(x);
            }
            seen.insert(weaponKey(jstr(rec, "weapon")));
            ++n;
        }
        return std::to_string(n) + " weapon stat record(s)";
    }, static_cast<int>(jinteger(rcfg_, "max_stats_pages", 8)));
    line("Done: stats for " + std::to_string(seen.size()) + " weapon(s), " + std::to_string(changes.size()) +
         " change(s) since the last reading.");
    Json out = Json::object();
    out["updated"] = utcNowIso();
    out["weapons"] = static_cast<long long>(seen.size());
    out["changes"] = changes;
    out["pages"] = pages;
    return out;
}

int Researcher::saveImages(const std::string& gameId, const std::string& url, const std::string& html, bool settings) {
    if (!rcfg_.get("save_screenshots").toBool(true)) return 0;
    int n = 0;
    for (auto& [imgUrl, alt] : pageImages(html, url, static_cast<size_t>(jinteger(rcfg_, "max_images_per_page", 3)), settings)) {
        auto img = image(imgUrl);
        if (!img) continue;
        try {
            shots_.save(gameId, img->bytes, img->mime, "", url, imgUrl, alt, "research", "", jnum(rcfg_, "screenshot_library_mb", 500),
                        settings ? "settings" : "build");
            ++n;
        } catch (...) {}
    }
    if (n) line("  saved " + std::to_string(n) + (settings ? " settings" : " build") + " screenshot(s)");
    return n;
}

}  // namespace hunter
