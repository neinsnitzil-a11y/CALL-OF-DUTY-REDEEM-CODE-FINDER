#include "stats.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

#include "research.hpp"
#include "util.hpp"

namespace hunter {

const std::vector<std::string> kStatFields = {"rpm", "ads_ms", "sprint_to_fire_ms", "velocity", "mag", "reload_s"};
const char* kStatsPrompt = R"(Game: %GAME%
Page: %URL%

Extract weapon statistics from this page as JSON. Use numbers only, exactly as the page gives them; leave out
anything the page doesn't state:
{"weapons": [{"weapon": "M4", "class": "Assault rifle", "rpm": 810,
  "damage": [[0, 34], [25, 28], [45, 24]],   // [start of range in metres, damage per bullet to the body]
  "ads_ms": 220, "sprint_to_fire_ms": 160, "velocity": 780, "mag": 30, "reload_s": 2.1}]}

Page text:
%TEXT%)";

namespace {
const std::map<std::string, int> kDefaultHealth = {{"mw4", 100}, {"mw3", 100}, {"mw2", 100}, {"mw2019", 100}, {"bo7", 150},
                                                   {"bo6", 150}, {"bocw", 150}, {"vanguard", 150}, {"warzone", 250}, {"codm", 100}};

// Python's patterns, minus the one look-ahead std::regex can't do (checked by hand for "mag").
const std::vector<std::pair<std::string, std::regex>>& patterns() {
    static const std::vector<std::pair<std::string, std::regex>> p = {
        {"rpm", re(R"((?:fire rate|rate of fire|rpm)\D{0,12}?(\d{3,4})|(\d{3,4})\s*rpm)", true)},
        {"ads_ms", re(R"(\bads(?: time| speed)?\D{0,10}?(\d{2,4})\s*ms)", true)},
        {"sprint_to_fire_ms", re(R"(sprint[- ]to[- ]fire\D{0,10}?(\d{2,4})\s*ms)", true)},
        {"velocity", re(R"((?:bullet )?velocity\D{0,10}?(\d{3,4})\s*m/s)", true)},
        {"mag", re(R"((?:magazine|mag size|ammo)\D{0,6}?(\d{1,3})\b(?!\s*(?:ms|m/s|rpm|m\b)))", true)},
    };
    return p;
}
const std::regex& damageRe() {
    // (\d{1,3})\s*(?:-|–|to)\s*(\d{1,3}|∞|inf)\s*m\D{0,6}?(\d{1,3})\b — \D is one byte here, so allow up to 12 (UTF-8)
    static const std::regex r = re("(\\d{1,3})\\s*(?:-|\xE2\x80\x93|to)\\s*(\\d{1,3}|\xE2\x88\x9E|inf)\\s*m\\D{0,6}?(\\d{1,3})\\b");
    return r;
}
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}
Json num(double v) {  // ints stay ints in the JSON
    return std::floor(v) == v ? Json(static_cast<long long>(v)) : Json(v);
}
}  // namespace

std::string weaponKey(const std::string& name) { return normKey(name); }

int healthFor(const Json& cfg, const std::string& gameId) {
    const Json& h = cfg.get("stats").get("health").get(gameId);
    if (h.isNumber() || h.isString()) return static_cast<int>(h.toInt(150));
    auto it = kDefaultHealth.find(gameId);
    return it == kDefaultHealth.end() ? 150 : it->second;
}

Json cleanStats(const Json& rec) {
    Json ok = Json::object();
    ok["weapon"] = truncateUtf8(trim(rec.get("weapon").toString()), 40);
    ok["class"] = truncateUtf8(rec.get("class").toString(), 30);
    static const std::vector<std::tuple<std::string, double, double>> bounds = {
        {"rpm", 30, 2000}, {"ads_ms", 50, 1500}, {"sprint_to_fire_ms", 30, 1000}, {"velocity", 100, 3000}, {"mag", 1, 300}, {"reload_s", 0.3, 15}};
    for (auto& [f, lo, hi] : bounds) {
        const Json& v = rec.get(f);
        if (!(v.isNumber() || v.isString())) continue;
        double d = v.toDouble(-1e9);
        if (d < lo || d > hi) continue;
        if (f == "reload_s") ok[f] = num(std::round(d * 100) / 100);
        else ok[f] = static_cast<long long>(d);
    }
    std::map<int, double> dmg;  // range → damage (last one wins, like Python's dict)
    for (const auto& pair : rec.get("damage").elements()) {
        if (!pair.isArray() || pair.size() < 2) continue;
        double r = pair.elements()[0].toDouble(-1), d = pair.elements()[1].toDouble(-1);
        if (r >= 0 && r <= 500 && d >= 1 && d <= 500) dmg[static_cast<int>(r)] = std::round(d * 10) / 10;
    }
    if (!dmg.empty()) {
        Json arr = Json::array();
        for (auto& [r, d] : dmg) {
            Json p = Json::array();
            p.push(r);
            p.push(num(d));
            arr.push(p);
        }
        ok["damage"] = arr;
    }
    return ok;
}

Json statsFromDocument(const Document& doc, const std::vector<std::string>& weapons) {
    std::vector<std::pair<std::string, Json>> out;  // insertion order
    std::string current;
    auto find = [&](const std::string& text) -> std::string {
        for (auto& w : weapons) if (containsWord(text, w)) return w;
        return "";
    };
    for (const auto& b : doc.blocks) {
        std::string head = find(b.heading);
        std::string lineW = b.text.size() < 120 ? find(b.text) : "";
        current = !lineW.empty() ? lineW : !head.empty() ? head : current;
        if (current.empty()) continue;
        auto it = std::find_if(out.begin(), out.end(), [&](auto& p) { return p.first == current; });
        if (it == out.end()) {
            Json r = Json::object();
            r["weapon"] = current;
            out.emplace_back(current, r);
            it = std::prev(out.end());
        }
        Json& rec = it->second;
        for (auto& [field, rx] : patterns()) {
            if (rec.contains(field)) continue;
            std::smatch m;
            std::string text = b.text;
            auto start = text.cbegin();
            bool found = false;
            while (std::regex_search(start, text.cend(), m, rx)) {
                std::string val;
                for (size_t g = 1; g < m.size(); ++g) if (m[g].matched) { val = m[g].str(); break; }
                if (field == "mag") {  // (?!\s*(?:ms|m/s|rpm|m\b)) after the number
                    size_t end = static_cast<size_t>(m[1].second - text.cbegin());
                    std::string tail = lower(text.substr(end));
                    size_t sp = tail.find_first_not_of(" \t");
                    std::string t = sp == std::string::npos ? "" : tail.substr(sp);
                    bool unit = startsWith(t, "ms") || startsWith(t, "m/s") || startsWith(t, "rpm") ||
                                (startsWith(t, "m") && (t.size() == 1 || !isAsciiAlnum(t[1])));
                    if (unit) { start = m[0].second; continue; }
                }
                rec[field] = val.find('.') != std::string::npos ? Json(std::stod(val)) : Json(std::stoll(val));
                found = true;
                break;
            }
            (void)found;
        }
        for (auto it2 = std::sregex_iterator(b.text.begin(), b.text.end(), damageRe()); it2 != std::sregex_iterator(); ++it2) {
            Json p = Json::array();
            p.push(std::stoi((*it2)[1].str()));
            p.push(std::stoi((*it2)[3].str()));
            rec["damage"].push(p);
        }
    }
    Json res = Json::array();
    for (auto& [w, r] : out) if (r.size() > 2) res.push(cleanStats(r));
    return res;
}

Json recordStats(Store& store, const std::string& gameId, const Json& recIn, const std::string& source) {
    Json rec = cleanStats(recIn);
    Json changes = Json::array();
    if (jstr(rec, "weapon").empty() || rec.size() <= 2) return changes;
    std::string key = weaponKey(jstr(rec, "weapon"));
    Json prev = store.weaponStat(gameId, key, source);
    if (prev.isObject()) {
        std::vector<std::string> fields = kStatFields;
        fields.push_back("damage");
        for (auto& f : fields) {
            if (rec.contains(f) && prev.contains(f) && !(rec.get(f) == prev.get(f)) &&
                !(rec.get(f).isNumber() && prev.get(f).isNumber() && rec.get(f).toDouble() == prev.get(f).toDouble())) {
                Json c = Json::array();
                c.push(f);
                c.push(prev.get(f));
                c.push(rec.get(f));
                changes.push(c);
                store.addStatChange(gameId, jstr(rec, "weapon"), f, prev.get(f).dump(), rec.get(f).dump(), source);
            }
        }
        Json merged = prev;
        for (const auto& [k, v] : rec.items()) merged[k] = v;
        store.putWeaponStat(gameId, jstr(rec, "weapon"), key, jstr(rec, "class"), merged.dump(), source, true);
    } else {
        store.putWeaponStat(gameId, jstr(rec, "weapon"), key, jstr(rec, "class"), rec.dump(), source, false);
    }
    return changes;
}

Json ttkOf(const Json& row, int health) {
    Json out = Json::object();
    const Json& dmg = row.get("damage");
    if (!dmg.isArray() || dmg.size() == 0) return out;
    double rpm = row.get("rpm").toDouble(0);
    const std::pair<const char*, const Json*> ends[] = {{"close", &dmg.elements().front()}, {"far", &dmg.elements().back()}};
    for (auto& [label, pair] : ends) {
        double d = pair->elements()[1].toDouble(0);
        if (!d) { out[std::string("stk_") + label] = Json(); continue; }
        long long stk = static_cast<long long>(std::ceil(health / d));
        out[std::string("stk_") + label] = stk;
        if (stk && rpm) out[std::string("ttk_") + label] = static_cast<long long>(std::nearbyint((stk - 1) * 60000.0 / rpm));
    }
    return out;
}

std::vector<std::pair<int, int>> ttkCurve(const Json& row, int health, int step, int maxRange) {
    std::vector<std::pair<int, int>> out;
    const auto& dmg = row.get("damage").elements();
    double rpm = row.get("rpm").toDouble(0);
    if (dmg.empty() || !rpm) return out;
    for (int m = 0; m <= maxRange; m += step) {
        double d = dmg.front().elements()[1].toDouble();
        for (auto it = dmg.rbegin(); it != dmg.rend(); ++it)
            if (m >= it->elements()[0].toDouble()) { d = it->elements()[1].toDouble(); break; }
        long long stk = static_cast<long long>(std::ceil(health / d));
        out.emplace_back(m, static_cast<int>(std::nearbyint((stk - 1) * 60000.0 / rpm)));
    }
    return out;
}

Json statsTable(Store& store, const std::string& gameId, int health) {
    std::vector<std::pair<std::string, std::vector<Json>>> by;
    const Json all = store.weaponStatsFor(gameId);
    for (const auto& r : all.elements()) {
        std::string k = jstr(r, "weapon_key");
        auto it = std::find_if(by.begin(), by.end(), [&](auto& p) { return p.first == k; });
        if (it == by.end()) { by.emplace_back(k, std::vector<Json>{}); it = std::prev(by.end()); }
        it->second.push_back(r);
    }
    std::vector<Json> rows;
    for (auto& [k, recs] : by) {
        std::vector<Json> stats;
        for (auto& r : recs) { try { stats.push_back(Json::parse(jstr(r, "stats"))); } catch (...) {} }
        Json row = Json::object();
        row["weapon"] = jstr(recs[0], "weapon");
        std::string cls;
        for (auto& r : recs) if (!jstr(r, "class").empty()) { cls = jstr(r, "class"); break; }
        row["class"] = cls;
        std::set<std::string> srcs;
        std::string updated;
        for (auto& r : recs) { srcs.insert(jstr(r, "source")); updated = std::max(updated, jstr(r, "seen_at")); }
        Json sa = Json::array();
        for (auto& x : srcs) sa.push(x);
        row["sources"] = sa;
        row["updated"] = updated;
        for (auto& f : kStatFields) {
            std::vector<double> vals;
            for (auto& st : stats) if (st.contains(f)) vals.push_back(st.get(f).toDouble());
            if (vals.empty()) continue;
            row[f] = num(median(vals));
            auto [mn, mx] = std::minmax_element(vals.begin(), vals.end());
            if (*mn != *mx) {
                Json spread = Json::array();
                spread.push(num(*mn));
                spread.push(num(*mx));
                row[f + "_spread"] = spread;
            } else {
                row[f + "_spread"] = Json();
            }
        }
        const Json* best = nullptr;
        for (auto& st : stats)
            if (st.get("damage").isArray() && (!best || st.get("damage").size() > best->get("damage").size())) best = &st;
        if (best && best->get("damage").size()) row["damage"] = best->get("damage");
        const Json t = ttkOf(row, health);
        for (const auto& [kk, v] : t.items()) row[kk] = v;
        rows.push_back(row);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Json& a, const Json& b) {
        bool na = !a.get("ttk_close").isNumber(), nb = !b.get("ttk_close").isNumber();
        return std::make_tuple(na, a.get("ttk_close").toDouble(0), jstr(a, "weapon")) <
               std::make_tuple(nb, b.get("ttk_close").toDouble(0), jstr(b, "weapon"));
    });
    Json out = Json::array();
    for (auto& r : rows) out.push(r);
    return out;
}

}  // namespace hunter
