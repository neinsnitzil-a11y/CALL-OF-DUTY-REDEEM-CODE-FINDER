#include "extraction.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <unordered_map>

#include "games.hpp"
#include "util.hpp"

namespace hunter {

namespace {
// ’ is three bytes in UTF-8, so apostrophes are written as (?:'|’).
const std::string AP = "(?:'|\xE2\x80\x99)";

const std::regex& CONTEXT_RE() {
    static const std::regex r = re(R"(redeem|redemption|reward|voucher|gift ?code|promo|\bcodes?\b|CODM|COD ?Mobile|Call of Duty: ?Mobile)", true);
    return r;
}
const std::regex& EXPIRED_HEADING_RE() {
    static const std::regex r = re("expired|no longer|not work|n" + AP + R"(?t\s+(?:\w+\s+){0,3}work|inactive|old codes|dead codes|)"
                                   R"(invalid codes|past codes|used codes|don)" + AP + R"(?t work)", true);
    return r;
}
const std::regex& FORMER_INTRO_RE() {
    static const std::regex r = re(R"(\bformer\b|\bexpired\b|no longer|\bold(?:er)? codes\b|not working|previous codes)", true);
    return r;
}
const std::regex& ACTIVE_INTRO_RE() {
    static const std::regex r = re(R"(\bnew\b|\bactive\b|\bworking\b|\blatest\b|\bvalid\b)", true);
    return r;
}
const std::regex& EXHAUSTED_RE() {
    static const std::regex r = re(
        R"(limit (?:has been )?reached|reached (?:its|the|their) (?:redemption |claim )?limits?|fully (?:redeemed|claimed)|)"
        R"(max(?:imum)? (?:number of )?(?:redemptions|claims)|all (?:\w+ ){0,2}(?:claimed|used up|gone)|)"
        R"(no (?:more )?(?:redemptions|claims) left|out of stock|used up|redemption cap)", true);
    return r;
}
const std::regex& EXAMPLE_LINE_RE() {
    static const std::regex r = re(
        R"(\b(?:e\.g\.|for example|example code|sample code|format (?:is|looks|like)|looks like this|)"
        R"(placeholder|misidentified|fake codes?|not a real code|scam|listicle)\b)", true);
    return r;
}
const std::regex& EXPIRED_LINE_RE() {
    static const std::regex r = re(R"(\bexpired\b|no longer (?:works?|valid|active)|\bnot working\b|\binactive\b)", true);
    return r;
}
const std::regex& URL_RE() {
    static const std::regex r = re(R"(https?://\S+)");
    return r;
}

const char* MONTHS = "January|February|March|April|May|June|July|August|September|October|November|December|"
                     "Jan|Feb|Mar|Apr|Jun|Jul|Aug|Sep|Sept|Oct|Nov|Dec";
const std::vector<std::regex>& EXPIRY_RE() {
    static const std::vector<std::regex> r = [] {
        const std::string exp = R"((?:expires?|expiry|expiration|valid (?:until|till|through)|ends?|until|before|by)\s*(?:on)?\s*[:\-]?\s*)";
        const std::string m = MONTHS;
        return std::vector<std::regex>{
            re(exp + "((?:" + m + R"()\.?\s+\d{1,2}(?:st|nd|rd|th)?,?\s+\d{4}))", true),
            re(exp + R"((\d{1,2}(?:st|nd|rd|th)?\s+(?:)" + m + R"()\.?,?\s+\d{4}))", true),
            re(exp + R"((\d{4}-\d{2}-\d{2}))", true),
        };
    }();
    return r;
}

bool inSet(char c, const char* extra) { return isAsciiAlnum(c) || (c && std::strchr(extra, c) != nullptr); }

int monthNumber(std::string word) {
    static const char* names[] = {"january", "february", "march", "april", "may", "june", "july",
                                  "august", "september", "october", "november", "december"};
    word = lower(word);
    for (int i = 0; i < 12; ++i) {
        std::string full = names[i];
        if (word == full || (word.size() == 3 && full.compare(0, 3, word) == 0)) return i + 1;
    }
    return 0;
}
bool validDate(int y, int m, int d) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1900 || m < 1 || m > 12 || d < 1) return false;
    int dim = days[m - 1] + (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0);
    return d <= dim;
}
std::optional<std::string> toIsoDate(std::string raw) {
    static const std::regex ordinal = re(R"((\d)(st|nd|rd|th))", true);
    static const std::regex sept = re(R"(\bSept\b)", true);
    raw = std::regex_replace(raw, ordinal, "$1");
    raw.erase(std::remove_if(raw.begin(), raw.end(), [](char c) { return c == ',' || c == '.'; }), raw.end());
    raw = std::regex_replace(raw, sept, "Sep");
    std::vector<std::string> parts;
    for (auto& p : split(collapseSpaces(raw), ' ')) if (!p.empty()) parts.push_back(p);
    char buf[32];
    if (parts.size() == 1) {  // 2026-11-30
        int y, m, d;
        if (std::sscanf(parts[0].c_str(), "%4d-%2d-%2d", &y, &m, &d) == 3 && validDate(y, m, d)) {
            std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
            return std::string(buf);
        }
        return std::nullopt;
    }
    if (parts.size() != 3) return std::nullopt;
    int m = monthNumber(parts[0]), d = 0, y = 0;
    try {
        if (m) { d = std::stoi(parts[1]); y = std::stoi(parts[2]); }
        else { m = monthNumber(parts[1]); d = std::stoi(parts[0]); y = std::stoi(parts[2]); }
    } catch (...) { return std::nullopt; }
    if (!m || !validDate(y, m, d)) return std::nullopt;
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
    return std::string(buf);
}

std::string lineRegion(const std::string& line, size_t codePos) {
    std::string tail = line.substr(codePos, 60);
    auto next = findTokens(tail, 1);
    if (!next.empty()) tail = tail.substr(0, next.front().start);
    return classifyRegion(findTokens(line).size() > 1 ? tail : line);
}
}  // namespace

Document Document::fromText(const std::string& text, const std::string& url, const std::string& title) {
    Document d{url, title, {}};
    for (const auto& line : splitLines(text)) {
        std::string t = collapseSpaces(line);
        if (!t.empty()) d.blocks.push_back({title, t});
    }
    return d;
}

bool hasCodeContext(const std::string& text) { return search(text, CONTEXT_RE()); }

bool isCodmShape(const std::string& code) {
    static const std::regex r = re(R"(^(?:[A-Z0-9]{4}Z[A-Z]Z[A-Z0-9]{3}|CODM[A-Z0-9]{7,9})$)");
    return std::regex_match(code, r);
}

std::string classifyRegion(const std::string& text) {
    static const std::regex g = re(R"(\bglobal\b|\bactivision\b)", true);
    static const std::regex ga = re(R"(\bgarena\b|\bSEA server\b|\bsoutheast asia\b)", true);
    bool a = search(text, g), b = search(text, ga);
    if (a && b) return "Both";
    return a ? "Global" : b ? "Garena" : "Unknown";
}

std::optional<std::string> parseExpiry(const std::string& text, std::optional<size_t> anchor) {
    struct Hit { size_t pos; std::string raw; };
    std::vector<Hit> hits;
    for (const auto& rx : EXPIRY_RE())
        for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it)
            hits.push_back({static_cast<size_t>(it->position()), (*it)[1].str()});
    if (anchor) {
        size_t a = *anchor;
        std::stable_sort(hits.begin(), hits.end(), [a](const Hit& x, const Hit& y) {
            auto dx = x.pos > a ? x.pos - a : a - x.pos;
            auto dy = y.pos > a ? y.pos - a : a - y.pos;
            return dx < dy;
        });
    }
    for (const auto& h : hits)
        if (auto d = toIsoDate(h.raw)) return d;
    return std::nullopt;
}

// [A-Z0-9]{6,24} not preceded by [A-Za-z0-9_\-/.@#] and not followed by [A-Za-z0-9_/@].
// Because the run is maximal, that's exactly Python's look-behind/look-ahead version.
std::vector<TokenMatch> findTokens(const std::string& text, size_t from) {
    std::vector<TokenMatch> out;
    size_t i = from;
    while (i < text.size()) {
        if (!isAsciiUpperOrDigit(text[i])) { ++i; continue; }
        size_t j = i;
        while (j < text.size() && isAsciiUpperOrDigit(text[j])) ++j;
        bool before = i == 0 || !inSet(text[i - 1], "_-/.@#");
        bool after = j == text.size() || !inSet(text[j], "_/@");
        size_t len = j - i;
        if (before && after && len >= 6 && len <= 24) out.push_back({i, len, text.substr(i, len)});
        i = j;
    }
    return out;
}

std::vector<TokenMatch> findDashed(const std::string& text) {
    static const std::regex body = re(R"([A-Z0-9]{4,5}(?:-[A-Z0-9]{4,5}){2,4}(?![A-Za-z0-9-]))");
    std::vector<TokenMatch> out;
    size_t i = 0;
    while (i < text.size()) {
        if (!isAsciiUpperOrDigit(text[i]) || (i > 0 && inSet(text[i - 1], "-"))) { ++i; continue; }
        std::smatch m;
        auto start = text.begin() + static_cast<long>(i);
        if (std::regex_search(start, text.end(), m, body, std::regex_constants::match_continuous)) {
            size_t len = static_cast<size_t>(m.length(0));
            out.push_back({i, len, m.str(0)});
            i += len;
        } else {
            ++i;
        }
    }
    return out;
}

std::vector<Candidate> extractFromDocument(const Document& doc) {
    const auto& blocks = doc.blocks;
    std::vector<std::string> headings;
    {
        std::set<std::string> seen;
        for (const auto& b : blocks) if (seen.insert(b.heading).second) headings.push_back(b.heading);
    }
    bool pageContext = search(doc.title, CONTEXT_RE()) || search(join(headings, " "), CONTEXT_RE());
    std::set<std::string> headingGames;
    for (const auto& h : headings) {
        std::string g = classifyGame({h});
        if (!g.empty()) headingGames.insert(g);
    }
    std::string docGame = classifyGame({doc.title});
    if (docGame.empty() && headingGames.size() == 1) docGame = *headingGames.begin();

    std::vector<Candidate> found;
    std::unordered_map<std::string, size_t> index;
    std::string currentHeading = "\x01";
    bool introExpired = false;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const auto& b = blocks[i];
        const std::string& line = b.text;
        if (b.heading != currentHeading) { currentHeading = b.heading; introExpired = false; }
        auto tokens = findTokens(line);
        auto dashed = findDashed(line);
        if (tokens.empty() && dashed.empty() && line.size() < 250) {
            if (search(line, FORMER_INTRO_RE())) introExpired = true;
            else if (search(line, ACTIVE_INTRO_RE())) introExpired = false;
            continue;
        }
        if (isScamLine(line) || search(line, EXAMPLE_LINE_RE())) continue;
        std::vector<std::pair<size_t, size_t>> urlSpans;
        for (auto it = std::sregex_iterator(line.begin(), line.end(), URL_RE()); it != std::sregex_iterator(); ++it)
            urlSpans.emplace_back(it->position(), it->position() + it->length());
        std::vector<TokenMatch> matches = tokens;
        matches.insert(matches.end(), dashed.begin(), dashed.end());
        std::stable_sort(matches.begin(), matches.end(), [](auto& a, auto& c) { return a.start < c.start; });
        std::string nearby;
        bool nearbyBuilt = false;
        bool headingExpired = search(b.heading, EXPIRED_HEADING_RE());
        bool headingExhausted = search(b.heading, EXHAUSTED_RE());
        bool singleToken = tokens.size() == 1;
        for (const auto& m : matches) {
            bool inUrl = std::any_of(urlSpans.begin(), urlSpans.end(),
                                     [&](auto& s) { return s.first <= m.start && m.start < s.second; });
            if (inUrl) continue;
            if (!pageContext) {
                if (!nearbyBuilt) {
                    std::vector<std::string> near;
                    for (size_t k = i >= 2 ? i - 2 : 0; k < std::min(blocks.size(), i + 3); ++k) near.push_back(blocks[k].text);
                    nearby = join(near, " ") + " " + b.heading;
                    nearbyBuilt = true;
                }
                if (!search(nearby, CONTEXT_RE())) continue;
            }
            std::string region = lineRegion(line, m.start);
            if (region == "Unknown") region = classifyRegion(b.heading);
            std::string tail = line.substr(std::min(line.size(), m.start + m.length), 40);
            bool lineExpired = search(tail, EXPIRED_LINE_RE()) || (singleToken && search(line, EXPIRED_LINE_RE()));
            bool lineExhausted = search(tail, EXHAUSTED_RE()) || (singleToken && search(line, EXHAUSTED_RE()));
            std::string report = (lineExhausted || headingExhausted) ? "exhausted"
                                 : (headingExpired || lineExpired || introExpired) ? "expired" : "active";
            Candidate c;
            c.code = m.code;
            c.gameHint = classifyGame({line, b.heading});
            if (c.gameHint.empty()) c.gameHint = docGame;
            c.platform = detectPlatform(m.code, {line.substr(m.start, m.length + 60), b.heading});
            c.kind = detectKind(m.code, {line, b.heading});
            c.snippet = truncateUtf8("[" + b.heading + "] " + line, 600);
            c.region = region;
            c.expiresAt = parseExpiry(line, m.start);
            c.report = report;
            auto it = index.find(m.code);
            if (it == index.end()) {
                index[m.code] = found.size();
                found.push_back(std::move(c));
            } else {  // listed twice on one page: dead reports and specific regions win
                static const std::map<std::string, int> rank = {{"active", 0}, {"expired", 1}, {"exhausted", 2}};
                Candidate& prev = found[it->second];
                if (rank.at(report) > rank.at(prev.report)) prev.report = report;
                if (prev.region == "Unknown") prev.region = region;
                if (!prev.expiresAt) prev.expiresAt = c.expiresAt;
            }
        }
    }
    return found;
}

std::vector<Candidate> extractCandidates(const std::string& text) { return extractFromDocument(Document::fromText(text)); }

}  // namespace hunter
