// Live weapon statistics (fire rate, damage by range, ADS, sprint-to-fire, velocity, magazine, reload)
// from the stats sites, read as text and by the AI, stored per site with a change history, combined
// across sites (median) and turned into shots-to-kill and time-to-kill.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "ai.hpp"
#include "extraction.hpp"
#include "json.hpp"
#include "store.hpp"

namespace hunter {

extern const std::vector<std::string> kStatFields;  // rpm, ads_ms, sprint_to_fire_ms, velocity, mag, reload_s
extern const char* kStatsPrompt;                    // %GAME% %URL% %TEXT%

std::string weaponKey(const std::string& name);
int healthFor(const Json& cfg, const std::string& gameId);
Json statsFromDocument(const Document& doc, const std::vector<std::string>& weapons);
Json cleanStats(const Json& rec);
// Saves one site's numbers; returns [[field, old, new]] that changed since that site's last reading.
Json recordStats(Store& store, const std::string& gameId, const Json& rec, const std::string& source);
Json statsTable(Store& store, const std::string& gameId, int health);  // one row per weapon, best TTK first
Json ttkOf(const Json& row, int health);
std::vector<std::pair<int, int>> ttkCurve(const Json& row, int health, int step = 5, int maxRange = 80);

}  // namespace hunter
