// config.json + .env, in the same places and formats as the Python version, so both read the
// same settings. Thread-safe: the scanner thread reads while the window saves.
#pragma once
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "json.hpp"

namespace hunter {

namespace fs = std::filesystem;

struct Paths {
    fs::path appDir;    // shipped files: default config.json, .env.example
    fs::path dataDir;   // your files: config.json, .env, database/, logs/, screenshots/
    fs::path config() const { return dataDir / "config.json"; }
    fs::path env() const { return dataDir / ".env"; }
    fs::path database() const { return dataDir / "database" / "hunter.sqlite3"; }
    fs::path logs() const { return dataDir / "logs"; }
    // %LOCALAPPDATA%\COD Redemption Hunter on Windows (shared with the Python installer),
    // ~/.local/share/cod-redemption-hunter elsewhere; HUNTER_DATA_DIR overrides.
    static fs::path defaultDataDir();
};

extern const std::vector<std::pair<std::string, std::string>> kEnvKeys;  // key → comment, display order
inline const char* kOfficialCodmRedemptionUrl = "https://www.callofduty.com/redemption";

class Config {
public:
    explicit Config(Paths paths);  // creates dataDir, copies shipped config.json / .env.example on first run
    const Paths& paths() const { return paths_; }

    Json snapshot() const;                  // a copy; safe to use from any thread
    void save(const Json& cfg);             // atomic write + replaces the in-memory copy
    void reload();
    void mergeShippedDefaults();            // new settings / sources from a newer shipped config

    std::string env(const std::string& key, const std::string& fallback = "") const;
    std::map<std::string, std::string> envValues() const;
    void writeEnv(const std::map<std::string, std::string>& updates);

    // convenience
    std::string redemptionUrl(const std::string& region) const;  // Global | Garena | "" (either)
    std::vector<std::string> accountRegions() const;
    int pollIntervalSeconds() const;
    Json runtime(const std::string& key) const;
    std::string version() const;

private:
    static std::map<std::string, std::string> parseEnvFile(const fs::path& p);
    Paths paths_;
    mutable std::mutex mu_;
    Json cfg_;
    std::map<std::string, std::string> env_;
};

std::string readFile(const fs::path& p);
void writeFileAtomic(const fs::path& p, const std::string& data);

}  // namespace hunter
