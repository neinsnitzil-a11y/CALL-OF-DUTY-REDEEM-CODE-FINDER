#include "validation.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <set>

#include "extraction.hpp"
#include "games.hpp"
#include "util.hpp"

namespace hunter {

std::string codeKey(const std::string& code) { return upper(replaceAll(code, "-", "")); }

std::string looksLikePlaceholder(const std::string& code) {
    static const std::regex ph = re("XXXX|0000|ABCD|1234|EXAMPLE|SAMPLE|YOURCODE|CODEHERE|ENTERCODE", true);
    std::string flat = codeKey(code);
    if (flat.empty()) return "";
    if (search(flat, ph)) return "placeholder / example code (e.g. XXXX-XXXXX-XXXX)";
    std::set<char> distinct(flat.begin(), flat.end());
    if (distinct.size() <= std::max<size_t>(2, flat.size() / 5)) return "placeholder: too few different characters";
    auto groups = split(upper(code), '-');
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](auto& g) { return g.empty(); }), groups.end());
    if (groups.size() > 1) {
        int same = 0;
        for (auto& g : groups) if (std::set<char>(g.begin(), g.end()).size() == 1) ++same;
        if (same >= 2) return "placeholder: repeated-letter groups";
    }
    return "";
}

namespace {
bool looksLikeWord(const std::string& token) {
    int vowels = 0;
    for (char c : token) if (std::string("AEIOU").find(c) != std::string::npos) ++vowels;
    double r = static_cast<double>(vowels) / static_cast<double>(token.size());
    return r >= 0.3 && r <= 0.6;
}
bool allOf(const std::string& s, int (*pred)(int)) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [&](char c) { return pred(static_cast<unsigned char>(c)) != 0; });
}
}  // namespace

Validation validateCode(const std::string& code, const Json& cfg) {
    const Json& icfg = cfg.get("intelligence");
    long long minLen = jinteger(icfg, "min_code_length", 8), maxLen = jinteger(icfg, "max_code_length", 20);
    std::set<std::string> blocklist;
    for (const auto& b : icfg.get("code_blocklist").elements()) blocklist.insert(upper(b.toString()));
    Validation v;
    std::string ph = looksLikePlaceholder(code);
    if (!ph.empty()) { v.reasons.push_back(ph); return v; }
    if (!code.empty() && (isActivisionDashed(code) || isXboxCode(code))) { v.valid = true; return v; }

    bool ascii = std::all_of(code.begin(), code.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    if (code.empty() || !ascii || !std::all_of(code.begin(), code.end(), isAsciiAlnum))
        v.reasons.push_back("not alphanumeric ASCII");
    auto len = static_cast<long long>(code.size());
    if (len < minLen || len > maxLen)
        v.reasons.push_back("length " + std::to_string(len) + " outside " + std::to_string(minLen) + "-" + std::to_string(maxLen));
    if (v.reasons.empty() && isCodmShape(code)) { v.valid = true; return v; }
    static const std::regex dateLike = re(R"(^(19|20)\d{6}$|^\d{8,}$)");
    static const std::regex versionLike = re(R"(^V?\d+(?:[A-Z]\d+)+$)");
    static const std::regex hex = re(R"(^[0-9A-F]+$)");
    if (allOf(code, std::isdigit) || std::regex_match(code, dateLike)) v.reasons.push_back("digits only (likely a UID, date or ID)");
    if (blocklist.count(upper(code))) v.reasons.push_back("blocklisted word");
    if (allOf(code, std::isalpha) && code.size() <= 12 && looksLikeWord(upper(code))) v.reasons.push_back("looks like a plain word");
    if (code.size() >= 16 && std::regex_match(code, hex)) {
        auto digits = std::count_if(code.begin(), code.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
        if (static_cast<double>(digits) > static_cast<double>(code.size()) * 0.4) v.reasons.push_back("looks like a hex hash");
    }
    if (std::regex_match(code, versionLike)) v.reasons.push_back("looks like a version/build number");
    if (!code.empty()) {
        std::map<char, int> counts;
        for (char c : code) ++counts[c];
        int most = 0;
        for (auto& [c, n] : counts) most = std::max(most, n);
        if (most > static_cast<double>(code.size()) * 0.5) v.reasons.push_back("too repetitive");
    }
    v.valid = v.reasons.empty();
    return v;
}

bool isExpired(const std::string& expiresAt) { return !expiresAt.empty() && expiresAt < todayIso(); }

// ── scoring ─────────────────────────────────────────────────────
const std::vector<std::string>& trustLevels() {
    static const std::vector<std::string> t = {"official", "established_publisher", "community", "unverified"};
    return t;
}

int baseScore(const std::string& trust, const Json& cfg) {
    std::map<std::string, int> scores = {{"official", 100}, {"established_publisher", 85}, {"community", 65}, {"unverified", 35}};
    for (const auto& [k, v] : cfg.get("source_scoring").items()) scores[k] = static_cast<int>(v.toInt(scores[k]));
    auto it = scores.find(trust);
    return it != scores.end() ? it->second : scores["unverified"];
}

int healthAdjusted(int score, int okCount, int errorCount) {
    int total = okCount + errorCount;
    if (total < 3) return score;
    double rate = static_cast<double>(errorCount) / total;
    return std::max(0, static_cast<int>(std::nearbyint(score - 15 * rate)));
}

int measuredScore(int score, int worked, int failed) {
    int n = worked + failed;
    if (n < 3) return score;
    double rate = (worked + 1.0) / (n + 2.0);
    double weight = n / (n + 2.0);
    return std::max(0, std::min(100, static_cast<int>(std::nearbyint(score * (1 - weight) + rate * 100 * weight))));
}

int codeConfidence(const std::vector<int>& sourceScores, const std::string& region, const std::string& expiresAt,
                   const Json& cfg, bool officialShape) {
    if (sourceScores.empty()) return 0;
    const Json& icfg = cfg.get("intelligence");
    long long per = jinteger(icfg, "corroboration_bonus_per_source", 5), cap = jinteger(icfg, "corroboration_bonus_cap", 15);
    long long score = *std::max_element(sourceScores.begin(), sourceScores.end());
    score += std::min<long long>(cap, per * static_cast<long long>(sourceScores.size() - 1));
    if (region != "Unknown") score += jinteger(icfg, "region_known_bonus", 3);
    if (!expiresAt.empty()) score += jinteger(icfg, "expiry_known_bonus", 2);
    score += officialShape ? jinteger(icfg, "codm_shape_bonus", 5) : -jinteger(icfg, "odd_shape_penalty", 20);
    return static_cast<int>(std::max<long long>(0, std::min<long long>(100, score)));
}

}  // namespace hunter
