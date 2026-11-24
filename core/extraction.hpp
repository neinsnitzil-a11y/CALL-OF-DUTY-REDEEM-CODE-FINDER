// Pulls candidate codes out of pages, keeping each code's heading and line so expired lists,
// regions ("(Garena version)") and expiry dates are read correctly.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace hunter {

struct Block {
    std::string heading;
    std::string text;
};

struct Document {
    std::string url, title;
    std::vector<Block> blocks;
    static Document fromText(const std::string& text, const std::string& url = "", const std::string& title = "");
};

struct Candidate {
    std::string code, snippet, region = "Unknown", report = "active";  // report: active | expired | exhausted
    std::string gameHint, platform, kind = "promo";
    std::optional<std::string> expiresAt;  // YYYY-MM-DD
};

struct TokenMatch {
    size_t start, length;
    std::string code;
};

bool isCodmShape(const std::string& code);
bool hasCodeContext(const std::string& text);  // redeem / reward / promo / codes / CODM …
std::string classifyRegion(const std::string& text);  // Global | Garena | Both | Unknown
std::optional<std::string> parseExpiry(const std::string& text, std::optional<size_t> anchor = std::nullopt);
std::vector<TokenMatch> findTokens(const std::string& text, size_t from = 0);   // plain caps codes
std::vector<TokenMatch> findDashed(const std::string& text);                     // SX4G-73D55-RNJ7 style
std::vector<Candidate> extractFromDocument(const Document& doc);
std::vector<Candidate> extractCandidates(const std::string& text);

}  // namespace hunter
