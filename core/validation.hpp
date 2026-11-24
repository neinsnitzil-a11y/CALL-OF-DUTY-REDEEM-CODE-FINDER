// Rejects malformed / implausible codes, and scores how much a listing can be trusted.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

namespace hunter {

struct Validation {
    bool valid = false;
    std::vector<std::string> reasons;
};

Validation validateCode(const std::string& code, const Json& cfg);
std::string looksLikePlaceholder(const std::string& code);  // reason, or "" if plausible
bool isExpired(const std::string& expiresAt);              // YYYY-MM-DD before today (UTC)
std::string codeKey(const std::string& code);               // dashes removed, upper-case

// ── source scoring ──────────────────────────────────────────────
int baseScore(const std::string& trust, const Json& cfg);
int healthAdjusted(int score, int okCount, int errorCount);
int measuredScore(int score, int worked, int failed);
int codeConfidence(const std::vector<int>& sourceScores, const std::string& region, const std::string& expiresAt,
                   const Json& cfg, bool officialShape = true);
const std::vector<std::string>& trustLevels();

}  // namespace hunter
