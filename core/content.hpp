// Builds & settings content: the curated builds file (data/meta.json), the research-site catalog
// (data/research_sources.json) and the screenshot library (with credit for every image).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "config.hpp"
#include "http.hpp"
#include "json.hpp"

namespace hunter {

// ── curated builds & settings ───────────────────────────────────
class MetaData {
public:
    explicit MetaData(const Paths& paths) : paths_(paths) {}
    Json load() const;                                   // newest valid copy (downloaded update or shipped)
    Json forGame(const std::string& gameId) const;       // {season, note, builds, live, settings, updated}
    std::vector<std::string> knownWeapons() const;       // longest first
    std::pair<bool, std::string> checkForUpdate(HttpClient& http, const std::string& url) const;
    static bool valid(const Json& data);

private:
    const Paths& paths_;
};
std::string buildText(const Json& build, const std::string& gameLabel = "");

// ── your own builds (data/my_builds.json: {"builds": [{id, game, weapon, class, role, mode, playstyle, range,
//    attachments: [[slot, name]], perks, equipment, notes, created, updated}]}) ─────────────────────
class MyBuilds {
public:
    explicit MyBuilds(const Paths& paths) : paths_(paths) {}
    Json all() const;                                   // every build, newest first
    Json forGame(const std::string& gameId) const;
    std::string save(const Json& build);                // new when it has no id; returns the id
    bool remove(const std::string& id);
    static Json clean(const Json& build);               // trims and checks one build; throws on a missing game/weapon
    std::string exportJson() const;
    int importJson(const std::string& text);            // adds (or replaces same id); returns how many

private:
    fs::path file() const { return paths_.dataDir / "data" / "my_builds.json"; }
    void write(const Json& list) const;
    const Paths& paths_;
};

// ── research sites catalog ──────────────────────────────────────
struct CatalogEntry {
    std::string name, url, note;
    bool crawl = false, gameSpecific = false;
    int priority = 2;
};
class SourceCatalog {
public:
    explicit SourceCatalog(const Paths& paths) : paths_(paths) {}
    Json load() const;
    std::vector<Json> all(const Json& cfg) const;  // built-in + yours, with "enabled" reflecting your choices
    std::vector<CatalogEntry> forGame(const Json& cfg, const std::string& gameId, const std::string& kind) const;

private:
    const Paths& paths_;
};

// ── screenshot library ──────────────────────────────────────────
// screenshots/<game>/web/<hash>.<ext> (found by research) and .../community/ (added by you), each with a
// <hash>.json next to it: weapon, source page, site, image URL, who added it, when. Same format as the
// Python edition, so both see the same library.
class ScreenshotLibrary {
public:
    explicit ScreenshotLibrary(const Paths& paths) : root_(paths.dataDir / "screenshots") {}
    const std::filesystem::path& root() const { return root_; }
    Json save(const std::string& gameId, const std::string& bytes, const std::string& mime, const std::string& weapon = "",
              const std::string& sourcePage = "", const std::string& imageUrl = "", const std::string& alt = "",
              const std::string& addedBy = "research", const std::string& note = "", double capMb = 500,
              const std::string& kind = "build");
    Json addFile(const std::string& gameId, const std::filesystem::path& file, const std::string& weapon = "",
                 const std::string& note = "");
    std::vector<Json> listing(const std::string& gameId = "", const std::string& weapon = "") const;
    void setWeapon(Json& meta, const std::string& weapon);
    void remove(const Json& meta);
    double sizeMb() const;
    int prune(double capMb);
    static std::string extFor(const std::string& mime);
    static std::string mimeFor(const std::filesystem::path& p);

private:
    std::filesystem::path root_;
};

std::string normKey(const std::string& s);  // lower-case letters and digits only

}  // namespace hunter
