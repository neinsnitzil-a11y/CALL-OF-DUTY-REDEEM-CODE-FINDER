#include "checker.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <regex>
#include <sstream>

#include "util.hpp"
#include "validation.hpp"

namespace hunter {

namespace {
struct Def { const char* verdict; int score; const char* reason; };
const std::map<std::string, Def>& definitive() {
    static const std::map<std::string, Def> d = {
        {"SUCCESS", {"REDEEMED BY YOU", 100, "The official page confirmed your redemption."}},
        {"ALREADY_REDEEMED", {"CLAIMED BY YOU", 100, "The official page says you already redeemed it."}},
        {"LIMIT_REACHED", {"DEAD · CLAIMS USED UP", 0, "The official page said its claim limit was reached."}},
        {"EXPIRED", {"DEAD · EXPIRED", 0, "Expired (official page, or its expiry date has passed)."}},
        {"INVALID", {"DEAD · INVALID", 0, "The official page said the code is invalid."}},
        {"REJECTED", {"NOT A CODE", 0, "Failed format checks."}},
        {"IGNORED", {"HIDDEN", -1, "You hid this code."}},
    };
    return d;
}
const std::string kTried = "Only the official redemption page can confirm it — use Redeem, and the result is recorded.";

std::optional<double> ageDays(const std::string& iso) {
    auto t = parseIso(iso);
    if (!t) return std::nullopt;
    return (static_cast<double>(nowSeconds()) - static_cast<double>(*t)) / 86400.0;
}
std::string lineOf(const std::string& snippet, const std::string& code) {
    size_t p = snippet.find("] ");
    std::string text = p == std::string::npos ? snippet : snippet.substr(p + 2);
    size_t i = text.find(code);
    return i == std::string::npos ? "" : text.substr(i, 80);
}
std::string fmt0(double v) {
    std::ostringstream s;
    s << static_cast<long long>(std::nearbyint(v));
    return s.str();
}
}  // namespace

bool isDeadVerdict(const std::string& v) { return v.find("DEAD") != std::string::npos; }
bool isLiveVerdict(const std::string& v) { return v == "LIKELY WORKS" || v == "UNCERTAIN"; }

Verdict assessCode(Store& store, const CodeRow& row) {
    auto d = definitive().find(row.status);
    if (d != definitive().end()) {
        Verdict v{d->second.verdict, d->second.score, {d->second.reason}};
        if (row.status == "EXPIRED" && !row.expiresAt.empty() && isExpired(row.expiresAt))
            v.reasons = {"Expiry date " + row.expiresAt + " has passed."};
        return v;
    }
    if (isExpired(row.expiresAt)) return {"DEAD · EXPIRED", 0, {"Expiry date " + row.expiresAt + " has passed."}};

    ReportCounts r = store.reportCounts(row.id);
    if (r.exhausted)
        return {"LIKELY DEAD · USED UP", 5,
                {std::to_string(r.exhausted) + " site(s) say its claims are used up (limit reached).", kTried}};
    if (r.expired)
        return {"LIKELY DEAD · EXPIRED", 10,
                {std::to_string(r.expired) + " site(s) list it as expired / not working" +
                     (r.active ? " (while " + std::to_string(r.active) + " still list it as working)." : "."),
                 kTried}};
    if (r.active == 0 && r.removed)
        return {"LIKELY DEAD · REMOVED", 15,
                {"Taken down by every site that listed it (" + std::to_string(r.removed) + ") — usually means it stopped working.",
                 kTried}};

    Verdict v;
    int score = 50 + std::min(20, 10 * r.active);
    v.reasons.push_back(r.active ? "Listed as working by " + std::to_string(r.active) + " site(s)."
                                 : "No site currently lists it as working.");
    if (r.removed) {
        score -= 10 * r.removed;
        v.reasons.push_back("Taken down by " + std::to_string(r.removed) + " site(s) that used to list it.");
    }
    auto age = ageDays(row.firstSeen);
    if (row.fresh) {
        if (age && *age < 1) { score += 15; v.reasons.push_back("Appeared less than a day ago — claims are usually still available."); }
        else if (age && *age < 3) { score += 5; v.reasons.push_back("Appeared " + fmt0(*age) + " day(s) ago."); }
        else if (age && *age < 7) { score -= 10; v.reasons.push_back("Appeared " + fmt0(*age) + " days ago — limited claims may be gone."); }
        else if (age) {
            score -= *age < 14 ? 25 : 35;
            v.reasons.push_back("Appeared " + fmt0(*age) + " days ago — codes this old are often used up.");
        }
    } else {
        score -= 10;
        v.reasons.push_back("Was already listed when the tool started watching, so its real age is unknown.");
    }
    static const std::regex newMark = re(R"(\(new!?\)|\bnew!?\b|\bjust (?:dropped|released)\b|\btoday\b)", true);
    for (const auto& s : store.allSnippets(row.id)) {
        if (search(lineOf(s, row.code), newMark)) { score += 10; v.reasons.push_back("A site marks it as new."); break; }
    }
    if (!row.expiresAt.empty()) { score += 5; v.reasons.push_back("Expires " + row.expiresAt + " (not yet)."); }
    if (row.region == "Garena") v.reasons.push_back("Garena code: redeem inside the game.");
    if (row.status == "UNKNOWN") { score -= 10; v.reasons.push_back("You tried it before but the result wasn't clear."); }
    if (row.status == "VALIDATED" && row.confidence < 50) {
        score -= 10;
        v.reasons.push_back("Only seen on low-trust sources / unusual format.");
    }
    v.score = std::max(0, std::min(100, score));
    v.verdict = v.score >= 65 ? "LIKELY WORKS" : v.score >= 35 ? "UNCERTAIN" : "LIKELY DEAD · OLD";
    v.reasons.push_back(kTried);
    return v;
}

void checkOne(Store& store, long long codeId) {
    auto row = store.getCode(codeId);
    if (!row || row->status == "REJECTED" || row->status == "IGNORED") return;
    Verdict v = assessCode(store, *row);
    store.saveCheck(codeId, v.verdict, v.score, join(v.reasons, " "));
}

int checkAll(Store& store) {
    int n = 0;
    for (const auto& row : store.listCodes(20000)) {
        if (row.status == "REJECTED" || row.status == "IGNORED") continue;
        Verdict v = assessCode(store, row);
        store.saveCheck(row.id, v.verdict, v.score, join(v.reasons, " "));
        ++n;
    }
    return n;
}

int verdictRank(const std::string& verdict) {
    static const std::vector<std::string> order = {
        "LIKELY WORKS", "REDEEMED BY YOU", "CLAIMED BY YOU", "UNCERTAIN", "LIKELY DEAD · OLD", "LIKELY DEAD · REMOVED",
        "LIKELY DEAD · EXPIRED", "LIKELY DEAD · USED UP", "DEAD · CLAIMS USED UP", "DEAD · EXPIRED", "DEAD · INVALID",
        "NOT A CODE", "HIDDEN"};
    for (size_t i = 0; i < order.size(); ++i) if (order[i] == verdict) return static_cast<int>(i);
    return 99;
}

std::vector<CodeRow> reportRows(Store& store, bool includeDead, const std::string& game) {
    std::vector<CodeRow> rows;
    for (auto& r : store.listCodes(20000)) {
        if (r.checkVerdict.empty() || r.status == "REJECTED" || r.status == "IGNORED") continue;
        if (!includeDead && isDeadVerdict(r.checkVerdict)) continue;
        if (!game.empty() && r.game != game) continue;
        rows.push_back(r);
    }
    std::stable_sort(rows.begin(), rows.end(), [](const CodeRow& a, const CodeRow& b) {
        int ra = verdictRank(a.checkVerdict), rb = verdictRank(b.checkVerdict);
        return ra != rb ? ra < rb : a.checkScore > b.checkScore;
    });
    return rows;
}

std::string toCsv(const std::vector<CodeRow>& rows) {
    auto cell = [](const std::string& v) {
        if (v.find_first_of(",\"\r\n") == std::string::npos) return v;
        return "\"" + replaceAll(v, "\"", "\"\"") + "\"";
    };
    std::string out = "\xEF\xBB\xBF";
    out += "code,game,region,platform,status,verdict,chance_it_works,first_seen,checked_at,reasons\r\n";
    for (const auto& r : rows) {
        std::vector<std::string> f = {r.code, r.game, r.region, r.platform, r.status, r.checkVerdict,
                                      r.checkScore >= 0 ? std::to_string(r.checkScore) : "", r.firstSeen, r.checkedAt,
                                      r.checkNotes};
        for (size_t i = 0; i < f.size(); ++i) out += (i ? "," : "") + cell(f[i]);
        out += "\r\n";
    }
    return out;
}

// ── rewards ─────────────────────────────────────────────────────
std::string rewardFrom(const std::string& snippet, const std::string& code) {
    static const std::regex headingPrefix = re(R"(^\[[^\]]*\]\s*)");
    static const std::regex sepStart = re("^\\s*(?:-|\xE2\x80\x93|\xE2\x80\x94|:|\\||\\()");
    static const std::regex nextCode = re(R"(\b[A-Z0-9]{8,}\b)");
    static const std::regex labels = re(R"(\((?:new!?|garena version|global version|expired|active)\)|\bnew!?\b)", true);
    static const std::regex lead = re("^(?:\\s|-|\xE2\x80\x93|\xE2\x80\x94|:|\\||\xC2\xB7|\xE2\x80\xA2|,)+");
    static const std::regex sentenceWords = re(R"(\b(?:is|are|was|were|almost|probably|filed)\b)");
    std::string line = std::regex_replace(snippet, headingPrefix, "", std::regex_constants::format_first_only);
    size_t i = line.find(code);
    if (i == std::string::npos) return "";
    std::string after = line.substr(i + code.size());
    if (line.size() > 180 && !std::regex_search(after, sepStart)) return "";
    std::smatch m;
    if (std::regex_search(after, m, nextCode)) after = after.substr(0, static_cast<size_t>(m.position(0)));
    after = std::regex_replace(after, labels, "");
    after = std::regex_replace(after, lead, "");
    // strip trailing " |-–—"
    while (!after.empty()) {
        if (after.back() == ' ' || after.back() == '|' || after.back() == '-') { after.pop_back(); continue; }
        if (endsWith(after, "\xE2\x80\x93") || endsWith(after, "\xE2\x80\x94")) { after.resize(after.size() - 3); continue; }
        break;
    }
    // first sentence at most: a lower-case letter then . ! ? and a space
    for (size_t k = 1; k + 1 < after.size(); ++k) {
        if ((after[k] == '.' || after[k] == '!' || after[k] == '?') && after[k - 1] >= 'a' && after[k - 1] <= 'z' &&
            std::isspace(static_cast<unsigned char>(after[k + 1]))) {
            after = after.substr(0, k);
            break;
        }
    }
    after = trim(after);
    size_t words = 0;
    for (auto& w : split(collapseSpaces(after), ' ')) if (!w.empty()) ++words;
    if (words > 12 || std::regex_search(after, sentenceWords)) return "";
    return truncateUtf8(after, 70);
}

std::string bestReward(const std::vector<std::string>& snippets, const std::string& code) {
    for (const auto& s : snippets) {
        std::string r = rewardFrom(s, code);
        if (r.size() > 2) return r;
    }
    return "";
}

}  // namespace hunter
