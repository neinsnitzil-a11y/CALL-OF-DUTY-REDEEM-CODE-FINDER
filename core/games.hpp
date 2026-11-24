// Every Call of Duty title, how its codes are redeemed, and which game / platform a text is about.
#pragma once
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace hunter {

struct Game {
    std::string id, name;
    int year = 0;
    std::vector<std::string> aliases;
    std::string redeem = "activision";  // codm | activision | store
    std::string promos;                 // official bonus-content page
    std::string shortName;
    const std::string& label() const { return shortName.empty() ? name : shortName; }
};

inline const char* kActivisionRedeemUrl = "https://www.callofduty.com/redeem";
inline const char* kBetaRedeemUrl = "https://www.callofduty.com/betaredeem";
inline const char* kXboxRedeemUrl = "https://www.xbox.com/redeem";

const std::vector<Game>& allGames();
const Game& gameById(const std::string& id);  // unknown → "other"
const std::vector<std::string>& currentTitles();  // codm, mw4, bo7, warzone, bo6

// First game named in the texts (checked in order, most specific first); "" if none.
std::string classifyGame(const std::vector<std::string>& texts);

bool isScamLine(const std::string& text);
bool isXboxCode(const std::string& code);
bool isActivisionDashed(const std::string& code);
bool isActivisionPlain(const std::string& code);
bool knownFormat(const std::string& code, const std::string& gameId);
std::string detectPlatform(const std::string& code, const std::vector<std::string>& texts);
std::string detectKind(const std::string& code, const std::vector<std::string>& texts);
std::pair<std::string, std::string> redeemInfo(const std::string& gameId, const std::string& code = "",
                                               const std::string& kind = "");

}  // namespace hunter
