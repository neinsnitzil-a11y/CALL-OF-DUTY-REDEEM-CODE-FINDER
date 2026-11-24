#include "util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace hunter {

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string collapseSpaces(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        // treat UTF-8 no-break space (C2 A0) as whitespace too
        bool nbsp = c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0;
        if (std::isspace(c) || nbsp) {
            if (nbsp) ++i;
            space = !out.empty();
            continue;
        }
        if (space) { out += ' '; space = false; }
        out += static_cast<char>(c);
    }
    return out;
}
std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            out.emplace_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}
std::vector<std::string> splitLines(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '\n') {
            std::string_view line = s.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            out.emplace_back(line);
            start = i + 1;
        }
    }
    return out;
}
std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}
bool startsWith(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
bool endsWith(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}
bool icontains(std::string_view hay, std::string_view needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != hay.end();
}
std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}
std::string truncateUtf8(const std::string& s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t n = maxBytes;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return s.substr(0, n);
}
bool isAsciiAlnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
bool isAsciiUpperOrDigit(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

namespace {
void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x110000) {
        out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
const std::unordered_map<std::string, unsigned>& namedEntities() {
    static const std::unordered_map<std::string, unsigned> m = {
        {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''}, {"nbsp", 0xA0},
        {"rsquo", 0x2019}, {"lsquo", 0x2018}, {"rdquo", 0x201D}, {"ldquo", 0x201C}, {"ndash", 0x2013},
        {"mdash", 0x2014}, {"hellip", 0x2026}, {"middot", 0xB7}, {"bull", 0x2022}, {"times", 0xD7},
        {"copy", 0xA9}, {"reg", 0xAE}, {"trade", 0x2122}, {"eacute", 0xE9}, {"rarr", 0x2192}};
    return m;
}
}  // namespace

std::string decodeEntities(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i + 1);
        if (semi == std::string_view::npos || semi - i > 12) { out += '&'; continue; }
        std::string name(s.substr(i + 1, semi - i - 1));
        unsigned cp = 0;
        bool ok = false;
        if (!name.empty() && name[0] == '#') {
            try {
                cp = (name.size() > 1 && (name[1] == 'x' || name[1] == 'X'))
                         ? static_cast<unsigned>(std::stoul(name.substr(2), nullptr, 16))
                         : static_cast<unsigned>(std::stoul(name.substr(1)));
                ok = true;
            } catch (...) {}
        } else {
            auto it = namedEntities().find(name);
            if (it != namedEntities().end()) { cp = it->second; ok = true; }
        }
        if (!ok) { out += '&'; continue; }
        appendUtf8(out, cp);
        i = semi;
    }
    return out;
}
std::string escapeHtml(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
    return out;
}

std::regex re(const std::string& pattern, bool icase) {
    auto flags = std::regex::ECMAScript | std::regex::optimize;
    if (icase) flags |= std::regex::icase;
    return std::regex(pattern, flags);
}
bool search(const std::string& text, const std::regex& rx) { return std::regex_search(text, rx); }
std::optional<std::smatch> firstMatch(const std::string& text, const std::regex& rx) {
    std::smatch m;
    if (std::regex_search(text, m, rx)) return m;
    return std::nullopt;
}

// ── time ────────────────────────────────────────────────────────
long long nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string utcIso(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%S+00:00", &tm);
    return buf;
}
std::string utcNowIso() { return utcIso(static_cast<std::time_t>(nowSeconds())); }
std::string todayIso() { return utcNowIso().substr(0, 10); }

namespace {
long long daysFromCivil(int y, unsigned m, unsigned d) {  // Howard Hinnant's algorithm, no timegm needed
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468;
}
}  // namespace

std::optional<std::time_t> parseIso(const std::string& iso) {
    int Y = 0, M = 0, D = 0, h = 0, mi = 0, sec = 0;
    if (iso.size() < 10 || std::sscanf(iso.c_str(), "%4d-%2d-%2d", &Y, &M, &D) != 3) return std::nullopt;
    if (iso.size() >= 19) std::sscanf(iso.c_str() + 11, "%2d:%2d:%2d", &h, &mi, &sec);
    long long t = daysFromCivil(Y, static_cast<unsigned>(M), static_cast<unsigned>(D)) * 86400LL + h * 3600 + mi * 60 + sec;
    // offset like +05:30 / -04:00 (Python writes +00:00)
    if (iso.size() >= 25 && (iso[19] == '+' || iso[19] == '-')) {
        int oh = 0, om = 0;
        if (std::sscanf(iso.c_str() + 20, "%2d:%2d", &oh, &om) == 2) {
            long long off = oh * 3600 + om * 60;
            t += iso[19] == '+' ? -off : off;
        }
    }
    return static_cast<std::time_t>(t);
}

// ── URLs ────────────────────────────────────────────────────────
Url parseUrl(const std::string& url) {
    Url u;
    size_t p = url.find("://");
    size_t start = 0;
    if (p != std::string::npos) { u.scheme = lower(url.substr(0, p)); start = p + 3; }
    size_t pathStart = url.find_first_of("/?#", start);
    u.host = lower(url.substr(start, pathStart == std::string::npos ? std::string::npos : pathStart - start));
    size_t at = u.host.rfind('@');
    if (at != std::string::npos) u.host = u.host.substr(at + 1);
    if (pathStart == std::string::npos) { u.path = "/"; return u; }
    std::string rest = url.substr(pathStart);
    size_t hash = rest.find('#');
    if (hash != std::string::npos) rest = rest.substr(0, hash);
    size_t q = rest.find('?');
    u.path = q == std::string::npos ? rest : rest.substr(0, q);
    if (u.path.empty()) u.path = "/";
    if (q != std::string::npos) u.query = rest.substr(q + 1);
    return u;
}
std::string urlDomain(const std::string& url) {
    std::string h = parseUrl(url).host;
    size_t colon = h.find(':');
    if (colon != std::string::npos) h = h.substr(0, colon);
    return startsWith(h, "www.") ? h.substr(4) : h;
}
std::string urlEncode(std::string_view s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isAsciiAlnum(static_cast<char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else if (c == ' ') out += '+';
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string urlDecode(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(std::string(s.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    return out;
}
std::string queryParam(const std::string& query, const std::string& key) {
    for (const auto& part : split(query, '&')) {
        size_t eq = part.find('=');
        if (eq != std::string::npos && part.substr(0, eq) == key) return urlDecode(part.substr(eq + 1));
    }
    return "";
}

std::string resolveUrl(const std::string& base, const std::string& hrefIn) {
    std::string href = trim(hrefIn);
    size_t hash = href.find('#');
    if (hash != std::string::npos) href = href.substr(0, hash);
    if (href.find("://") != std::string::npos) {
        size_t colon = href.find(':');
        std::string scheme = lower(href.substr(0, colon));
        return scheme == "http" || scheme == "https" ? href : "";
    }
    if (startsWith(href, "javascript:") || startsWith(href, "mailto:") || startsWith(href, "data:") || startsWith(href, "tel:"))
        return "";
    Url b = parseUrl(base);
    std::string origin = (b.scheme.empty() ? "https" : b.scheme) + "://" + b.host;
    if (startsWith(href, "//")) return (b.scheme.empty() ? "https" : b.scheme) + ":" + href;
    if (href.empty()) return origin + b.path + (b.query.empty() ? "" : "?" + b.query);
    if (href[0] == '?') return origin + b.path + href;
    std::string path;
    if (href[0] == '/') path = href;
    else {
        std::string dir = b.path.substr(0, b.path.rfind('/') + 1);
        path = dir + href;
    }
    // normalise ./ and ../ (query left alone)
    std::string query;
    size_t q = path.find('?');
    if (q != std::string::npos) { query = path.substr(q); path = path.substr(0, q); }
    std::vector<std::string> out;
    auto parts = split(path, '/');
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto& p = parts[i];
        if (p == ".") { if (i + 1 == parts.size()) out.push_back(""); continue; }
        if (p == "..") { if (out.size() > 1) out.pop_back(); if (i + 1 == parts.size()) out.push_back(""); continue; }
        out.push_back(p);
    }
    std::string joined = join(out, "/");
    if (joined.empty() || joined[0] != '/') joined = "/" + joined;
    return origin + joined + query;
}

std::string base64Encode(std::string_view data) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i + 1]) << 8) |
                     static_cast<unsigned char>(data[i + 2]);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i < data.size()) {
        unsigned v = static_cast<unsigned char>(data[i]) << 16;
        if (i + 1 < data.size()) v |= static_cast<unsigned char>(data[i + 1]) << 8;
        out += t[v >> 18]; out += t[(v >> 12) & 63];
        out += i + 1 < data.size() ? t[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// ── SHA-256 (FIPS 180-4) ────────────────────────────────────────
namespace {
constexpr std::array<uint32_t, 64> K = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
}  // namespace

std::string sha256Hex(std::string_view data) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string msg(data);
    uint64_t bits = static_cast<uint64_t>(msg.size()) * 8;
    msg += static_cast<char>(0x80);
    while (msg.size() % 64 != 56) msg += '\0';
    for (int i = 7; i >= 0; --i) msg += static_cast<char>((bits >> (i * 8)) & 0xFF);
    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const auto* p = reinterpret_cast<const unsigned char*>(msg.data() + chunk + i * 4);
            w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) std::snprintf(out + i * 8, 9, "%08x", h[i]);
    return std::string(out, 64);
}

}  // namespace hunter
