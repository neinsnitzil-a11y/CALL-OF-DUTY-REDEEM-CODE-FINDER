// AI helper chat: ask anything (gaming or not), answered live.
// Events while it works:  line (progress) · status · image (a screenshot from your library or a page)
//                         · source (url, title) · text (the next piece of the answer)
// With web lookups on, providers that can search the web themselves do (Claude, Gemini, OpenRouter,
// Perplexity, OpenAI search models); otherwise it reads the game's build/settings pages, Google News and
// Wikipedia for free and gives the AI those as numbered sources to cite.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "ai.hpp"
#include "config.hpp"
#include "content.hpp"
#include "research.hpp"
#include "store.hpp"

namespace hunter {

struct WebItem {
    std::string title, url, text, date;
};
std::vector<WebItem> newsSearch(HttpClient& http, const std::string& query, size_t limit = 6);
std::vector<WebItem> wikipediaSearch(HttpClient& http, const std::string& query, size_t limit = 2, const std::string& lang = "en");
std::vector<std::string> urlsIn(const std::string& text, size_t limit = 3);

using HelperEvent = std::function<void(const std::string& kind, const Json& data)>;

class Helper {
public:
    Helper(Config& cfg, Store& store, HttpClient& http);
    MetaData meta;
    SourceCatalog catalog;
    ScreenshotLibrary shots;
    ResearchStore research;

    bool isCod(const std::string& question, const std::string& gameId) const;
    std::vector<std::string> weaponsIn(const std::string& text, const std::string& gameId) const;
    std::string appContext(const std::string& gameId) const;

    // Returns {answer, sources:[url], source_titles:{url:title}, images:[meta], search}
    Json ask(AiClient& ai, const std::string& question, const std::string& gameId, bool web, const AiHistory& history,
             const std::vector<AiImage>& images, const HelperEvent& onEvent);

    // chat memory (survives restarts) and saved answers — same files as the Python edition
    Json loadChat() const;
    void saveChat(const Json& turns) const;
    Json saveAnswer(const std::string& question, const Json& result, const std::string& gameId) const;
    std::vector<Json> savedAnswers() const;

    std::function<std::string(const std::string&)> renderer;
    std::function<void(double)> sleeper;

private:
    void codPages(Researcher& r, const std::string& question, const std::string& gameId, std::vector<std::string>& blocks,
                  const std::function<int(const std::string&, const std::string&)>& source,
                  const std::function<void(const Json&)>& show, const HelperEvent& onEvent);
    Config& cfg_;
    Store& store_;
    HttpClient& http_;
};

}  // namespace hunter
