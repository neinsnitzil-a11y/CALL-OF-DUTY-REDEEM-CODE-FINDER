// Discord studio: choose exactly what goes to Discord — working codes, the not-posted-yet queue, best
// builds, researched builds, settings, screenshots or your own / AI-written message — preview it, pick the
// channel, send. Your own screenshots are uploaded as files; screenshots found on sites are shown from the
// site's own image link, always with credit (site + source page).
#pragma once
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.hpp"
#include "discord.hpp"
#include "http.hpp"
#include "json.hpp"
#include "store.hpp"

namespace hunter {

struct StudioMessage {
    Json payload;                                                    // what Discord receives
    std::vector<std::tuple<std::string, std::string, std::string>> files;  // (name, bytes, mime) uploads
};

Json buildEmbed(const Json& build, const std::string& gameLabel, bool researched);
Json settingsEmbed(const Json& section, const std::string& gameLabel, const std::string& title);
Json shotEmbed(const Json& meta, const std::string& gameLabel, const std::string& uploadName = "");

// kind: codes | queue | builds | research | settings | shots | custom.
// items: code ids (codes/queue) or the build / row / section / screenshot objects (others).
std::vector<StudioMessage> studioMessages(const Config& cfg, Store& store, const std::string& kind, const std::string& gameId,
                                          const Json& items, const std::string& content, bool mention,
                                          const std::string& settingsTitle = "");
std::string studioPreview(const std::vector<StudioMessage>& msgs);
std::string studioDestination(const Config& cfg, const std::string& dest, const std::string& gameId, const std::string& custom = "");
std::pair<std::string, std::string> multipartBody(const Json& payload,
                                                  const std::vector<std::tuple<std::string, std::string, std::string>>& files);
// Sends every message (pausing between them). Codes posted from the queue are marked as posted.
std::pair<bool, std::string> studioSend(Config& cfg, Store& store, HttpClient& http, const std::string& url,
                                        const std::vector<StudioMessage>& msgs, const std::string& kind,
                                        const std::vector<long long>& codeIds = {});

}  // namespace hunter
