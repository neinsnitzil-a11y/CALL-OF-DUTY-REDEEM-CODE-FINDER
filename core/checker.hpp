// Code checker (verdict + "chance it still works") and reward-label reading.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "store.hpp"

namespace hunter {

struct Verdict {
    std::string verdict;
    int score = -1;  // -1 = not applicable (hidden codes)
    std::vector<std::string> reasons;
};

Verdict assessCode(Store& store, const CodeRow& row);
int checkAll(Store& store);                    // stores a verdict on every code; returns how many
void checkOne(Store& store, long long codeId);
int verdictRank(const std::string& verdict);   // best first, for sorting
bool isDeadVerdict(const std::string& v);
bool isLiveVerdict(const std::string& v);

// Stored verdicts, best first (the check report). includeDead=false drops DEAD / LIKELY DEAD codes.
std::vector<CodeRow> reportRows(Store& store, bool includeDead = true, const std::string& game = "");
std::string toCsv(const std::vector<CodeRow>& rows);  // UTF-8 with BOM so Excel shows emoji/accents right

std::string rewardFrom(const std::string& snippet, const std::string& code);
std::string bestReward(const std::vector<std::string>& snippets, const std::string& code);

}  // namespace hunter
