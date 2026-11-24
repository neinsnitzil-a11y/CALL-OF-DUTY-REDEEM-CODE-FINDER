// String, time, URL and hashing helpers shared by the core.
#pragma once
#include <ctime>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace hunter {

// ── strings ─────────────────────────────────────────────────────
std::string trim(std::string_view s);
std::string upper(std::string s);
std::string lower(std::string s);
std::string collapseSpaces(std::string_view s);  // runs of whitespace → one space, trimmed
std::vector<std::string> split(std::string_view s, char sep);
std::vector<std::string> splitLines(std::string_view s);
std::string join(const std::vector<std::string>& parts, std::string_view sep);
bool startsWith(std::string_view s, std::string_view p);
bool endsWith(std::string_view s, std::string_view p);
bool icontains(std::string_view hay, std::string_view needle);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);
std::string truncateUtf8(const std::string& s, size_t maxBytes);  // never cuts a multibyte character
bool isAsciiAlnum(char c);
bool isAsciiUpperOrDigit(char c);
std::string decodeEntities(std::string_view s);  // &amp; &#39; &#x2019; &nbsp; …
std::string escapeHtml(std::string_view s);

// Regex helpers (std::regex has no look-behind, so boundaries are checked by hand).
using RegexFlags = std::regex_constants::syntax_option_type;
std::regex re(const std::string& pattern, bool icase = false);
bool search(const std::string& text, const std::regex& rx);
std::optional<std::smatch> firstMatch(const std::string& text, const std::regex& rx);

// ── time (UTC, ISO-8601 "YYYY-MM-DDTHH:MM:SS+00:00" like the Python version) ──
std::string utcNowIso();
std::string utcIso(std::time_t t);
std::string todayIso();                         // YYYY-MM-DD (UTC)
std::optional<std::time_t> parseIso(const std::string& iso);  // accepts both +00:00 and Z
long long nowSeconds();

// ── URLs ────────────────────────────────────────────────────────
struct Url {
    std::string scheme, host, path, query;  // path starts with "/"
};
Url parseUrl(const std::string& url);
std::string urlDomain(const std::string& url);  // lower-case host without "www."
std::string urlEncode(std::string_view s);       // application/x-www-form-urlencoded (spaces → +)
std::string urlDecode(std::string_view s);
std::string queryParam(const std::string& query, const std::string& key);

std::string resolveUrl(const std::string& base, const std::string& href);  // like urljoin; drops #fragment

// ── hashing / encoding ─────────────────────────────────────────────────────
std::string sha256Hex(std::string_view data);
std::string base64Encode(std::string_view data);

}  // namespace hunter
