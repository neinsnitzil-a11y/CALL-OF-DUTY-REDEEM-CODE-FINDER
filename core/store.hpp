// SQLite data layer — same schema (PRAGMA user_version 7) as the Python version, so an existing
// hunter.sqlite3 opens as-is. One connection guarded by a mutex; WAL + busy timeout so the
// dashboard, the scanner thread and a separate service process can share the file.
#pragma once
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

struct sqlite3;

namespace hunter {

struct CodeRow {
    long long id = 0;
    std::string code, region = "Unknown", firstSeen, lastSeen, expiresAt, status = "DISCOVERED";
    int confidence = 0, sourceCount = 0;
    std::string validationNotes, notes;
    bool fresh = false;
    std::string checkVerdict, checkNotes, checkedAt, discordState;
    int checkScore = -1;
    std::string game = "codm", platform, kind = "promo";
    std::string snippet;  // filled by codesView()
};

struct SourceRow {
    long long id = 0;
    std::string name, url, type = "web", trust = "unverified";
    int baseScore = 0;
    std::string lastChecked, lastStatus, etag, lastModified, lastContentAt;
    bool lastStatusNull = true;
    int okCount = 0, errorCount = 0, codesFound = 0;
};

struct Sighting {
    long long id = 0, codeId = 0, sourceId = 0;
    std::string documentUrl, seenAt, snippet, report = "active", sourceName, trust;
};

struct LogRow {
    long long id = 0;
    std::string ts, name, status, details;  // scan_log: name/status; audit_log: status = event
};

struct AttemptRow {
    long long id = 0, codeId = 0;
    std::string code, region, startedAt, submittedAt, outcome, method, evidence;
    bool counted = false;
};

struct ReportCounts {
    int active = 0, expired = 0, exhausted = 0, removed = 0;
};

extern const std::vector<std::string> kStatuses;
bool isFinalStatus(const std::string& status);

class Store {
public:
    explicit Store(const std::string& path);  // ":memory:" works for tests
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    int schemaVersion();

    // sources
    SourceRow registerSource(const std::string& name, const std::string& url, const std::string& type,
                             const std::string& trust, int score);
    std::optional<SourceRow> getSource(const std::string& url);
    void updateSourceHealth(const std::string& url, bool ok, const std::string& status, const std::string& etag = "",
                            const std::string& lastModified = "", int codesFound = 0);
    std::vector<SourceRow> listSources();
    void setContentTime(const std::string& url);

    // codes & sightings
    std::optional<CodeRow> getCodeByText(const std::string& code);
    std::optional<CodeRow> getCode(long long id);
    long long insertCode(const std::string& code, const std::string& region, const std::string& expiresAt,
                         const std::string& status, int confidence, const std::string& notes,
                         const std::string& game = "codm", const std::string& platform = "", const std::string& kind = "promo");
    void refineCode(long long id, const std::string& game, const std::string& platform, const std::string& kind);
    void touchCode(long long id, const std::string& region, const std::string& expiresAt);
    void addSighting(long long codeId, long long sourceId, const std::string& documentUrl, const std::string& snippet,
                     const std::string& report = "active");
    ReportCounts reportCounts(long long codeId);
    int activeSightingsOn(long long sourceId, const std::string& documentUrl);
    std::vector<long long> markRemoved(long long sourceId, const std::string& documentUrl,
                                       const std::vector<std::string>& presentCodes);
    std::vector<std::string> allSnippets(long long codeId);
    void setDiscordState(const std::vector<long long>& ids, const std::string& state);
    void saveCheck(long long id, const std::string& verdict, int score, const std::string& notes);
    std::map<long long, std::pair<int, int>> sourceTrackRecord();          // source id → (worked, failed)
    std::vector<std::pair<long long, int>> sightingSources(long long codeId);  // (source id, base score)
    std::vector<Sighting> sightingsFor(long long codeId);
    void updateCode(long long id, const std::map<std::string, std::string>& textFields,
                    const std::map<std::string, long long>& intFields = {});
    void setStatus(long long id, const std::string& status, const std::string& notes = "");
    std::vector<CodeRow> listCodes(int limit = 10000);
    std::vector<CodeRow> codesView(const std::string& view, const std::string& search = "", const std::string& game = "");
    std::map<std::string, int> viewCounts(const std::string& game = "");
    std::map<std::string, std::pair<int, int>> gameCounts();                // game → (ready, total)
    int expirePastDue();
    std::map<std::string, int> stats();
    static const std::vector<std::string>& viewNames();

    // redemption attempts
    long long startAttempt(long long codeId, const std::string& uidHash, const std::string& region);
    void markSubmitted(long long attemptId);
    void finishAttempt(long long attemptId, const std::string& outcome, const std::string& method, const std::string& evidence);
    int countedAttemptsSince(const std::string& uidHash, const std::string& sinceIso);
    std::string lastSubmission(const std::string& uidHash);
    std::vector<AttemptRow> recentAttempts(int limit = 200);

    // weapon stats, patches, your match results (schema v7 tables; rows as JSON objects)
    Json weaponStat(const std::string& game, const std::string& key, const std::string& source);  // null if none
    void putWeaponStat(const std::string& game, const std::string& weapon, const std::string& key, const std::string& cls,
                       const std::string& statsJson, const std::string& source, bool update);
    void addStatChange(const std::string& game, const std::string& weapon, const std::string& field, const std::string& oldJson,
                       const std::string& newJson, const std::string& source);
    Json statChanges(const std::string& game, const std::string& since = "", int limit = 200);
    Json weaponStatsFor(const std::string& game);
    bool insertPatch(const std::string& game, const std::string& title, const std::string& url, const std::string& published,
                     const std::string& changesJson, bool balance);  // true = new
    Json patch(const std::string& game, const std::string& url);
    void updatePatchChanges(const std::string& game, const std::string& url, const std::string& changesJson, bool balance);
    Json patchesSince(const std::string& game, const std::string& sinceIso);
    void addBuildRun(const std::string& game, const std::string& weapon, const std::string& key, const std::string& mode,
                     int kills, int deaths, bool won, int placement, const std::string& notes);
    Json buildRuns(const std::string& game = "", const std::string& key = "", int limit = 100000);

    // state & logs
    void setState(const std::string& key, const std::string& value);
    std::string getState(const std::string& key, const std::string& fallback = "");
    void logScan(const std::string& name, const std::string& status, const std::string& details = "");
    std::vector<LogRow> recentScans(int limit = 100);
    void addAudit(const std::string& event, const std::string& details = "");
    std::vector<LogRow> recentAudit(int limit = 200);

private:
    class Stmt;
    void exec(const std::string& sql);
    void migrate();
    std::vector<CodeRow> queryCodes(const std::string& sql, const std::vector<std::string>& args, bool withSnippet = false);
    sqlite3* db_ = nullptr;
    std::recursive_mutex mu_;
};

}  // namespace hunter
