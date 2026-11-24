// Builds & settings research: reads the game's build / settings guides (your pages, hand-picked deep
// pages, then the research-site catalog, crawling hub sites for this game's pages), pulls builds and
// settings out of the text (and with an AI, out of the text and screenshots), saves build screenshots
// with credit, and cross-checks: a weapon listed by 2+ different sites is "verified".
// robots.txt is respected, HTTPS only, a polite delay between pages. Results: data/research.json.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ai.hpp"
#include "content.hpp"
#include "extraction.hpp"
#include "html.hpp"
#include "http.hpp"
#include "store.hpp"

namespace hunter {

std::string canonSlot(const std::string& s);
Json buildsFromDocument(const Document& doc, const std::vector<std::string>& weapons);   // [{weapon, attachments, via}]
Json settingsFromDocument(const Document& doc);                                         // [{section, setting, value, why, via}]
std::vector<std::string> findLinks(const std::string& html, const std::string& baseUrl, const std::string& gameId,
                                   const std::string& kind, size_t limit = 3, bool gameSpecific = false);
std::vector<std::pair<std::string, std::string>> pageImages(const std::string& html, const std::string& baseUrl,
                                                            size_t limit, bool settingsImages = false);  // (url, alt)
Json crossCheck(const Json& found, const Json& curated);   // one row per weapon with agreement
Json mergeSettings(const Json& found);                    // one row per setting with value votes
std::string docText(const Document& doc, size_t limit);
std::string researchText(const Json& row, const std::string& gameLabel = "");
bool containsWord(const std::string& text, const std::string& word);  // case-insensitive, whole word

class ResearchStore {
public:
    explicit ResearchStore(const Paths& p) : path_(p.dataDir / "data" / "research.json") {}
    Json load() const;
    Json forGame(const std::string& gameId) const;
    void save(const std::string& gameId, const std::string& part, const Json& result);

private:
    std::filesystem::path path_;
};

class Researcher {
public:
    Researcher(const Json& cfg, HttpClient& http, const MetaData& meta, const SourceCatalog& catalog,
               ScreenshotLibrary& shots, AiClient* ai = nullptr, std::function<void(const std::string&)> onLine = {});

    struct PlanEntry {
        std::string name, url, note;
        bool crawl = false, gameSpecific = true;
    };
    std::vector<PlanEntry> plan(const std::string& gameId, const std::string& kind) const;
    // handle(html, finalUrl, entry) → short note. Returns the per-page report [{url, ok, note}].
    Json walk(const std::string& gameId, const std::string& kind,
              const std::function<std::string(const std::string&, const std::string&, const PlanEntry&)>& handle,
              int maxPages = 0);
    std::pair<std::string, std::string> fetch(const std::string& url);              // (html, final url) or throws
    std::optional<AiImage> image(const std::string& url);                            // budgeted download
    Json researchBuilds(const std::string& gameId, bool useImages = true);
    Json researchSettings(const std::string& gameId, bool useImages = true);
    // Weapon statistics from the stats sites → weapon_stats (with change history). Returns {weapons, changes, pages}.
    Json researchStats(const std::string& gameId, Store& store);
    int saveImages(const std::string& gameId, const std::string& url, const std::string& html, bool settings);

    std::function<std::string(const std::string&)> renderer;  // JavaScript pages (optional)
    std::function<void(double)> sleeper;
    const std::atomic<bool>* stop = nullptr;

private:
    std::vector<std::string> pagesFor(const std::string& gameId, const std::string& kind) const;
    bool allowed(const std::string& url);
    Json aiBuilds(const std::string& label, const std::string& url, const Document& doc);
    Json imageBuilds(const std::string& gameId, const std::string& label, const std::string& url, const std::string& html,
                     const std::vector<std::string>& weapons);
    void line(const std::string& s) { if (onLine_) onLine_(s); }

    Json cfg_, rcfg_;
    HttpClient& http_;
    const MetaData& meta_;
    const SourceCatalog& catalog_;
    ScreenshotLibrary& shots_;
    AiClient* ai_;
    std::function<void(const std::string&)> onLine_;
    std::map<std::string, Robots> robots_;
    int imageBudget_, renderBudget_;
};

}  // namespace hunter
