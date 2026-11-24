// Community builds on GitHub — free, no server. Your community repo (config community.repo = "owner/name"):
//   a build = an issue with a hidden JSON block · a vote = 👍/👎 reaction · a creator = the GitHub account
//   a shared result = a comment with a "perf" block · moderation = close/lock or the "verified" label
// Without GITHUB_TOKEN, submitting and voting open GitHub in your browser pre-filled; reading needs no token.
// Also: ntfy phone push, and playstyle recommendations + your own match tracking.
#pragma once
#include <string>
#include <vector>

#include "config.hpp"
#include "http.hpp"
#include "json.hpp"
#include "store.hpp"

namespace hunter {

class CommunityError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

double wilson(int up, int down, double z = 1.96);

class Community {
public:
    Community(Config& cfg, HttpClient& http) : cfg_(cfg), http_(http) {}
    std::string repo() const;
    static std::pair<std::string, std::string> buildIssue(const Json& build, const std::string& gameId, const std::string& note = "");
    std::string submitUrl(const Json& build, const std::string& gameId, const std::string& note = "") const;
    std::string submit(const Json& build, const std::string& gameId, const std::string& note = "");
    void vote(long long number, bool up);
    void shareResults(long long number, const Json& summary);
    static Json parseIssue(const Json& issue);  // null if it isn't a build post
    Json refresh(bool force = false, int maxPages = 3);
    Json buildsFor(const std::string& gameId = "", const std::string& sort = "top", const std::string& playstyle = "") const;
    Json resultsFor(long long number);
    Json creatorLinks();
    Json creators(const Json& builds) const;
    Json newFromFollowed(Store& store, const Json& builds) const;
    Json loadCache() const;

private:
    Json call(const std::string& method, const std::string& path, const Json* payload, std::string* etag = nullptr);
    void saveCache(const Json& c) const;
    Config& cfg_;
    HttpClient& http_;
};

// ── ntfy phone push ─────────────────────────────────────────────
std::string newPushTopic();
std::string pushServer(const Config& cfg);
std::string pushSubscribeUrl(const Config& cfg);
std::pair<bool, std::string> pushSend(const Config& cfg, HttpClient& http, const std::string& title, const std::string& message,
                                      const std::string& click = "", const std::string& tags = "dart", int priority = 4,
                                      const std::string& event = "codes");

// ── playstyle recommendations & your match results ──────────────
struct Playstyle {
    std::string id, label, range;
    std::vector<std::pair<std::string, int>> classes;
};
const std::vector<Playstyle>& playstyles();
Json playerProfile(const Json& cfg);
std::string weaponClassOf(const std::string& text);
std::string weaponRangeOf(const std::string& text);
std::string buildKey(const std::string& gameId, const std::string& weapon);
void logMatch(Store& store, const std::string& gameId, const std::string& weapon, int kills, int deaths, bool won,
              const std::string& mode = "", int placement = 0, const std::string& notes = "");
Json performance(Store& store, const std::string& gameId = "");  // {build_key: {...}, "*": overall}
Json recommend(Config& cfg, Store& store, const Json& candidates, const std::string& gameId, size_t limit = 8,
               const Json& profile = Json());
// Every build we know for a game: curated, researched, community — in one shape.
Json buildCandidates(const Config& cfg, const std::string& gameId, const Json& communityBuilds);

}  // namespace hunter
