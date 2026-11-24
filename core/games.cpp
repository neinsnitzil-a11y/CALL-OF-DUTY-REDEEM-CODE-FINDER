#include "games.hpp"

#include <algorithm>
#include <regex>
#include <tuple>

#include "extraction.hpp"
#include "util.hpp"

namespace hunter {

const std::vector<Game>& allGames() {
    static const std::vector<Game> games = {
        {"codm", "Call of Duty: Mobile", 2019,
         {"Call of Duty: Mobile", "Call of Duty Mobile", "COD Mobile", "CODM", "COD: Mobile", "Garena"}, "codm", "", "CODM"},
        {"mw4", "Modern Warfare 4", 2026, {"Modern Warfare 4", "MW4", "MWIV"}, "activision",
         "https://support.activision.com/modern-warfare-4/articles/modern-warfare-4-bonus-content", ""},
        {"bo7", "Black Ops 7", 2025, {"Black Ops 7", "BO7"}, "activision",
         "https://support.activision.com/black-ops-7/articles/black-ops-7-bonus-content", ""},
        {"warzone", "Warzone", 2020, {"Warzone", "Verdansk", "Rebirth Island", "Urzikstan", "Area 99"}, "activision", "", ""},
        {"bo6", "Black Ops 6", 2024, {"Black Ops 6", "BO6"}, "activision",
         "https://support.activision.com/black-ops-6/articles/black-ops-6-bonus-content", ""},
        {"mw3", "Modern Warfare III (2023)", 2023, {"Modern Warfare III", "Modern Warfare 3 (2023)", "MWIII", "MW3"},
         "activision", "https://support.activision.com/modern-warfare-iii/articles/modern-warfare-iii-bonus-content",
         "MW III (2023)"},
        {"mw2", "Modern Warfare II (2022)", 2022, {"Modern Warfare II", "Modern Warfare 2 (2022)", "MWII", "MW2"},
         "activision", "", "MW II (2022)"},
        {"vanguard", "Vanguard", 2021, {"Call of Duty: Vanguard", "COD Vanguard", "Vanguard"}, "activision", "", ""},
        {"bocw", "Black Ops Cold War", 2020, {"Black Ops Cold War", "Cold War", "BOCW"}, "activision", "", ""},
        {"mw2019", "Modern Warfare (2019)", 2019, {"Modern Warfare (2019)", "Modern Warfare 2019", "MW2019"},
         "activision", "", "MW (2019)"},
        {"bo4", "Black Ops 4", 2018, {"Black Ops 4", "Black Ops IIII", "BO4", "Blackout"}, "activision", "", ""},
        {"wwii", "WWII", 2017, {"Call of Duty: WWII", "COD WWII", "COD WW2"}, "store", "", ""},
        {"iw", "Infinite Warfare", 2016, {"Infinite Warfare"}, "store", "", ""},
        {"bo3", "Black Ops III", 2015, {"Black Ops III", "Black Ops 3", "BO3"}, "store", "", ""},
        {"aw", "Advanced Warfare", 2014, {"Advanced Warfare"}, "store", "", ""},
        {"ghosts", "Ghosts", 2013, {"Call of Duty: Ghosts", "COD Ghosts"}, "store", "", ""},
        {"bo2", "Black Ops II", 2012, {"Black Ops II", "Black Ops 2", "BO2"}, "store", "", ""},
        {"mw3_2011", "Modern Warfare 3 (2011)", 2011, {"Modern Warfare 3 (2011)"}, "store", "", "MW3 (2011)"},
        {"bo1", "Black Ops (2010)", 2010, {"Call of Duty: Black Ops (2010)"}, "store", "", "Black Ops (2010)"},
        {"mw2_2009", "Modern Warfare 2 (2009)", 2009, {"Modern Warfare 2 (2009)"}, "store", "", "MW2 (2009)"},
        {"waw", "World at War", 2008, {"World at War"}, "store", "", ""},
        {"cod4", "Call of Duty 4: Modern Warfare", 2007, {"Call of Duty 4", "COD4"}, "store", "", "CoD 4: MW"},
        {"cod3", "Call of Duty 3", 2006, {"Call of Duty 3"}, "store", "", ""},
        {"cod2", "Call of Duty 2", 2005, {"Call of Duty 2"}, "store", "", ""},
        {"cod1", "Call of Duty (2003)", 2003, {"Call of Duty (2003)", "United Offensive"}, "store", "", "CoD (2003)"},
        {"other", "Other / unsorted", 0, {}, "activision", "", "Other"},
    };
    return games;
}

const Game& gameById(const std::string& id) {
    const auto& g = allGames();
    const std::string want = id.empty() ? "codm" : id;
    for (const auto& x : g) if (x.id == want) return x;
    return g.back();
}

const std::vector<std::string>& currentTitles() {
    static const std::vector<std::string> t = {"codm", "mw4", "bo7", "warzone", "bo6"};
    return t;
}

namespace {
struct Alias {
    std::regex rx;
    std::string gameId;
    size_t len;
};

std::string escapeRegex(const std::string& s) {
    static const std::string special = R"(\^$.|?*+()[]{}/)";
    std::string out;
    for (char c : s) {
        if (c == ' ') { out += "\\s*"; continue; }
        if (special.find(c) != std::string::npos) out += '\\';
        out += c;
    }
    return out;
}

const std::vector<Alias>& aliases() {
    static const std::vector<Alias> list = [] {
        std::vector<std::pair<std::string, std::string>> raw;
        for (const auto& g : allGames())
            for (const auto& a : g.aliases) raw.emplace_back(a, g.id);
        std::stable_sort(raw.begin(), raw.end(), [](auto& a, auto& b) { return a.first.size() > b.first.size(); });
        std::vector<Alias> out;
        for (auto& [a, id] : raw) out.push_back({re(escapeRegex(a), a.size() > 4), id, a.size()});
        return out;
    }();
    return list;
}

// First match of rx in text whose ends aren't glued to other letters/digits (Python's look-arounds).
std::optional<std::pair<size_t, size_t>> boundedSearch(const std::string& text, const std::regex& rx) {
    for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it) {
        size_t a = static_cast<size_t>(it->position()), b = a + static_cast<size_t>(it->length());
        if (it->length() == 0) continue;
        if (a > 0 && isAsciiAlnum(text[a - 1])) continue;
        if (b < text.size() && isAsciiAlnum(text[b])) continue;
        return std::make_pair(a, b - a);
    }
    return std::nullopt;
}
}  // namespace

std::string classifyGame(const std::vector<std::string>& texts) {
    for (const auto& text : texts) {
        if (text.empty()) continue;
        std::optional<std::tuple<size_t, long, std::string>> best;
        for (const auto& a : aliases()) {
            auto m = boundedSearch(text, a.rx);
            if (!m) continue;
            std::tuple<size_t, long, std::string> hit{m->first, -static_cast<long>(m->second), a.gameId};
            if (!best || hit < *best) best = hit;
        }
        if (best) return std::get<2>(*best);
    }
    return "";
}

bool isScamLine(const std::string& text) {
    static const std::regex rx = re(R"((?:free|unlimited)\s+(?:cod\s*points|cp)\b|cp generator|points generator)", true);
    return search(text, rx);
}
bool isXboxCode(const std::string& code) {
    static const std::regex rx = re(R"(^[A-Z0-9]{5}(?:-[A-Z0-9]{5}){4}$)");
    return std::regex_match(code, rx);
}
bool isActivisionDashed(const std::string& code) {
    static const std::regex rx = re(R"(^[A-Z0-9]{4,5}-[A-Z0-9]{5}-[A-Z0-9]{4,5}$)");
    return std::regex_match(code, rx);
}
bool isActivisionPlain(const std::string& code) {
    static const std::regex rx = re(R"(^(?=.*\d)[A-Z0-9]{13}$)");
    return std::regex_match(code, rx);
}
bool knownFormat(const std::string& code, const std::string& gameId) {
    if (gameId == "codm") return isCodmShape(code);
    return isActivisionDashed(code) || isActivisionPlain(code) || isXboxCode(code);
}

std::string detectPlatform(const std::string& code, const std::vector<std::string>& texts) {
    if (isXboxCode(code)) return "Xbox";
    static const std::vector<std::pair<std::string, std::regex>> platforms = {
        {"Xbox", re(R"(\bxbox\b|\bseries [xs]\b|game pass|microsoft store)", true)},
        {"PlayStation", re(R"(\bplaystation\b|\bps[45]\b|\bpsn\b)", true)},
        {"PC", re(R"(\bpc\b|\bsteam\b|battle\.net|\bbattlenet\b)", true)},
        {"Switch", re(R"(\bswitch\s*2?\b|\bnintendo\b)", true)},
    };
    for (const auto& text : texts) {
        if (text.empty()) continue;
        std::vector<std::string> found;
        for (const auto& [name, rx] : platforms) if (search(text, rx)) found.push_back(name);
        if (!found.empty()) return join(found, " / ");
    }
    return "All platforms";
}

std::string detectKind(const std::string& code, const std::vector<std::string>& texts) {
    if (isXboxCode(code)) return "xbox";
    static const std::regex beta = re(R"(\bbeta\b|early access)", true);
    for (const auto& t : texts) if (!t.empty() && search(t, beta)) return "beta";
    return "promo";
}

std::pair<std::string, std::string> redeemInfo(const std::string& gameId, const std::string& code, const std::string& kind) {
    const Game& g = gameById(gameId);
    if (kind == "beta" && g.redeem != "codm")
        return {kBetaRedeemUrl, "Beta / early-access code: redeem at callofduty.com/betaredeem, then use the platform code you're emailed."};
    if (isXboxCode(code))
        return {kXboxRedeemUrl, "Xbox / Microsoft Store code: redeem it at xbox.com/redeem or in the Xbox app."};
    if (g.redeem == "codm")
        return {"", "CODM code: use Redeem (UID + CAPTCHA), or redeem Garena codes inside the game."};
    if (g.redeem == "store")
        return {kActivisionRedeemUrl, g.name + " doesn't use web codes for in-game content — its DLC comes from the "
                                               "platform store. Any Activision code found is redeemed at callofduty.com/redeem."};
    return {kActivisionRedeemUrl, "Redeem at callofduty.com/redeem: sign in to your Activision account, paste the "
                                  "code, and pick the platform you play on (Xbox, PlayStation or PC)."};
}

}  // namespace hunter
