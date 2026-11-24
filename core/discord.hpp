// Discord webhooks: colour-coded code cards, the current list when a webhook is connected,
// new codes as they appear, and a follow-up when a posted code dies. Delivery state is kept per
// code, so nothing is posted twice and failures retry next scan.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "http.hpp"
#include "json.hpp"
#include "store.hpp"

namespace hunter {

bool validWebhook(const std::string& url);
std::string statusText(const CodeRow& row);
int colorFor(const CodeRow& row);
std::string webhookFor(const Config& cfg, const std::string& gameId, const std::string& purpose = "codes");
Json codeEmbed(const Config& cfg, const std::vector<std::string>& snippets, const CodeRow& row, const std::string& kind);

struct SyncResult {
    int posted = 0, updates = 0, listed = 0;
    std::string error;
};

class DiscordNotifier {
public:
    DiscordNotifier(Config& cfg, Store& store, HttpClient& http, std::string overrideUrl = "")
        : cfg_(cfg), store_(store), http_(http), override_(std::move(overrideUrl)) {}

    SyncResult sync();                                                   // after every scan
    std::pair<int, std::string> postList(bool includeDead = false, const std::string& game = "");
    std::pair<bool, std::string> test(const std::string& url = "");
    std::pair<bool, std::string> post(const std::string& url, const Json& payload, int tries = 3);
    void systemAlert(const std::string& event, const std::string& details);

    std::function<void(double)> sleeper;  // tests skip the waits

private:
    Json opts() const;
    std::string route(const std::string& game, const std::string& purpose) const;
    Json message(const std::string& content, const Json& embeds) const;
    std::pair<std::vector<long long>, std::string> send(const std::string& url, const std::string& content,
                                                        const std::vector<CodeRow>& rows, const std::string& kind);
    void log(bool ok, const std::string& text);
    bool wanted(const CodeRow& r) const;
    std::vector<CodeRow> rows();
    std::pair<int, std::string> postGameList(const std::string& game, const std::vector<CodeRow>& rows,
                                             const std::string& url, bool includeDead, bool connect);
    void wait(double s);
    Config& cfg_;
    Store& store_;
    HttpClient& http_;
    std::string override_;
};

}  // namespace hunter
