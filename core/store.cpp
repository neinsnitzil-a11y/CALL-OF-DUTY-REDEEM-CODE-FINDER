#include "store.hpp"

#include <sqlite3.h>

#include <ctime>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "util.hpp"
#include "validation.hpp"

namespace hunter {

const std::vector<std::string> kStatuses = {
    "DISCOVERED", "VALIDATED", "READY", "LIKELY_EXPIRED", "LIKELY_EXHAUSTED", "SUBMITTED", "SUCCESS", "EXPIRED",
    "LIMIT_REACHED", "INVALID", "ALREADY_REDEEMED", "REGION_MISMATCH", "UNKNOWN", "REJECTED", "IGNORED"};

bool isFinalStatus(const std::string& s) {
    static const std::set<std::string> f = {"SUCCESS", "EXPIRED", "INVALID", "ALREADY_REDEEMED", "LIMIT_REACHED", "REJECTED", "IGNORED"};
    return f.count(s) > 0;
}

namespace {
const char* MIGRATIONS[] = {
    R"(
    CREATE TABLE IF NOT EXISTS codes(
      id INTEGER PRIMARY KEY, code TEXT NOT NULL UNIQUE, region TEXT NOT NULL DEFAULT 'Unknown',
      first_seen TEXT NOT NULL, last_seen TEXT NOT NULL, expires_at TEXT,
      status TEXT NOT NULL DEFAULT 'DISCOVERED', confidence INTEGER NOT NULL DEFAULT 0,
      source_count INTEGER NOT NULL DEFAULT 0, validation_notes TEXT DEFAULT '', notes TEXT DEFAULT '');
    CREATE INDEX IF NOT EXISTS idx_codes_status ON codes(status);
    CREATE TABLE IF NOT EXISTS sources(
      id INTEGER PRIMARY KEY, name TEXT NOT NULL, url TEXT NOT NULL, type TEXT NOT NULL DEFAULT 'web',
      trust TEXT NOT NULL, base_score INTEGER NOT NULL, last_checked TEXT, last_status TEXT,
      ok_count INTEGER NOT NULL DEFAULT 0, error_count INTEGER NOT NULL DEFAULT 0,
      codes_found INTEGER NOT NULL DEFAULT 0, etag TEXT, last_modified TEXT, UNIQUE(url));
    CREATE TABLE IF NOT EXISTS sightings(
      id INTEGER PRIMARY KEY, code_id INTEGER NOT NULL REFERENCES codes(id) ON DELETE CASCADE,
      source_id INTEGER REFERENCES sources(id), document_url TEXT, seen_at TEXT NOT NULL, snippet TEXT,
      UNIQUE(code_id, source_id, document_url));
    CREATE TABLE IF NOT EXISTS redemption_attempts(
      id INTEGER PRIMARY KEY, code_id INTEGER NOT NULL REFERENCES codes(id), account_uid_hash TEXT NOT NULL,
      region TEXT, started_at TEXT NOT NULL, submitted_at TEXT, counted INTEGER NOT NULL DEFAULT 0,
      outcome TEXT NOT NULL DEFAULT 'PENDING', verification_method TEXT DEFAULT '', evidence TEXT DEFAULT '');
    CREATE INDEX IF NOT EXISTS idx_attempts_uid ON redemption_attempts(account_uid_hash, submitted_at);
    CREATE TABLE IF NOT EXISTS app_state(key TEXT PRIMARY KEY, value TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS scan_log(
      id INTEGER PRIMARY KEY, scanned_at TEXT NOT NULL, source_name TEXT NOT NULL, status TEXT NOT NULL, details TEXT DEFAULT '');
    CREATE TABLE IF NOT EXISTS audit_log(id INTEGER PRIMARY KEY, ts TEXT NOT NULL, event TEXT NOT NULL, details TEXT DEFAULT '');
    )",
    "ALTER TABLE sightings ADD COLUMN report TEXT NOT NULL DEFAULT 'active';",
    R"(ALTER TABLE codes ADD COLUMN fresh INTEGER NOT NULL DEFAULT 0;
    UPDATE codes SET fresh=1 WHERE id IN (SELECT DISTINCT g.code_id FROM sightings g
        JOIN sources s ON s.id=g.source_id WHERE s.type IN ('manual','search-result'));)",
    R"(ALTER TABLE codes ADD COLUMN check_verdict TEXT DEFAULT '';
    ALTER TABLE codes ADD COLUMN check_score INTEGER;
    ALTER TABLE codes ADD COLUMN check_notes TEXT DEFAULT '';
    ALTER TABLE codes ADD COLUMN checked_at TEXT;
    ALTER TABLE sources ADD COLUMN last_content_at TEXT;)",
    R"(ALTER TABLE codes ADD COLUMN discord_state TEXT NOT NULL DEFAULT '';
    ALTER TABLE codes ADD COLUMN discord_sent_at TEXT;)",
    R"(ALTER TABLE codes ADD COLUMN game TEXT NOT NULL DEFAULT 'codm';
    ALTER TABLE codes ADD COLUMN platform TEXT NOT NULL DEFAULT '';
    ALTER TABLE codes ADD COLUMN kind TEXT NOT NULL DEFAULT 'promo';
    CREATE INDEX IF NOT EXISTS idx_codes_game ON codes(game, status);
    ALTER TABLE codes ADD COLUMN code_key TEXT;
    UPDATE codes SET code_key=REPLACE(UPPER(code), '-', '');
    CREATE INDEX IF NOT EXISTS idx_codes_key ON codes(code_key);)",
    R"(CREATE TABLE IF NOT EXISTS weapon_stats(
      id INTEGER PRIMARY KEY, game TEXT NOT NULL, weapon TEXT NOT NULL, weapon_key TEXT NOT NULL, class TEXT DEFAULT '',
      stats TEXT NOT NULL, source TEXT DEFAULT '', seen_at TEXT NOT NULL, UNIQUE(game, weapon_key, source));
    CREATE TABLE IF NOT EXISTS stat_changes(
      id INTEGER PRIMARY KEY, game TEXT NOT NULL, weapon TEXT NOT NULL, field TEXT NOT NULL, old TEXT, new TEXT,
      source TEXT DEFAULT '', seen_at TEXT NOT NULL);
    CREATE TABLE IF NOT EXISTS patches(
      id INTEGER PRIMARY KEY, game TEXT NOT NULL, title TEXT NOT NULL, url TEXT NOT NULL, published TEXT,
      seen_at TEXT NOT NULL, weapon_changes TEXT NOT NULL DEFAULT '[]', balance INTEGER NOT NULL DEFAULT 0,
      UNIQUE(game, url));
    CREATE TABLE IF NOT EXISTS build_runs(
      id INTEGER PRIMARY KEY, game TEXT NOT NULL, weapon TEXT NOT NULL, build_key TEXT NOT NULL, mode TEXT DEFAULT '',
      kills INTEGER NOT NULL DEFAULT 0, deaths INTEGER NOT NULL DEFAULT 0, won INTEGER NOT NULL DEFAULT 0,
      placement INTEGER, notes TEXT DEFAULT '', played_at TEXT NOT NULL);
    CREATE INDEX IF NOT EXISTS idx_runs_build ON build_runs(build_key);)",
};

const std::vector<std::pair<std::string, std::string>>& viewFilters() {
    static const std::vector<std::pair<std::string, std::string>> v = {
        {"ready", "status='READY'"},
        {"fresh", "fresh=1 AND status NOT IN ('REJECTED','IGNORED')"},
        {"garena", "region IN ('Garena','Both') AND status IN ('READY','VALIDATED','LIKELY_EXPIRED','UNKNOWN')"},
        {"maybe", "status IN ('VALIDATED','UNKNOWN','REGION_MISMATCH','DISCOVERED')"},
        {"expired", "status IN ('LIKELY_EXPIRED','LIKELY_EXHAUSTED','EXPIRED','LIMIT_REACHED','INVALID')"},
        {"redeemed", "status IN ('SUCCESS','ALREADY_REDEEMED','SUBMITTED')"},
        {"all", "status NOT IN ('REJECTED','IGNORED')"},
    };
    return v;
}
std::string filterFor(const std::string& view) {
    for (auto& [k, w] : viewFilters()) if (k == view) return w;
    return viewFilters().back().second;
}
const std::set<std::string>& updatableColumns() {
    static const std::set<std::string> c = {"region", "expires_at", "status", "confidence", "validation_notes", "notes",
                                            "fresh", "check_verdict", "check_score", "check_notes", "checked_at",
                                            "discord_state", "discord_sent_at", "game", "platform", "kind", "last_seen"};
    return c;
}
}  // namespace

// ── prepared statement wrapper ──────────────────────────────────
class Store::Stmt {
public:
    Stmt(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &st_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("SQL error: ") + sqlite3_errmsg(db) + " in: " + sql.substr(0, 120));
    }
    ~Stmt() { sqlite3_finalize(st_); }
    Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(st_, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
    Stmt& bind(int i, const char* v) { return bind(i, std::string(v)); }
    Stmt& bind(int i, long long v) { sqlite3_bind_int64(st_, i, v); return *this; }
    Stmt& bind(int i, int v) { sqlite3_bind_int64(st_, i, v); return *this; }
    Stmt& bindNull(int i) { sqlite3_bind_null(st_, i); return *this; }
    Stmt& bindOpt(int i, const std::string& v) { return v.empty() ? bindNull(i) : bind(i, v); }
    bool step() {
        int rc = sqlite3_step(st_);
        if (rc == SQLITE_ROW) {
            if (cols_.empty())
                for (int c = 0; c < sqlite3_column_count(st_); ++c) cols_[sqlite3_column_name(st_, c)] = c;
            return true;
        }
        if (rc != SQLITE_DONE) throw std::runtime_error(std::string("SQL step: ") + sqlite3_errmsg(db_));
        return false;
    }
    int col(const std::string& name) const {
        auto it = cols_.find(name);
        return it == cols_.end() ? -1 : it->second;
    }
    std::string text(const std::string& name) const {
        int c = col(name);
        if (c < 0) return "";
        auto p = sqlite3_column_text(st_, c);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
    long long integer(const std::string& name, long long fallback = 0) const {
        int c = col(name);
        if (c < 0 || sqlite3_column_type(st_, c) == SQLITE_NULL) return fallback;
        return sqlite3_column_int64(st_, c);
    }
    bool isNull(const std::string& name) const {
        int c = col(name);
        return c < 0 || sqlite3_column_type(st_, c) == SQLITE_NULL;
    }
    int changes() const { return sqlite3_changes(db_); }

private:
    sqlite3* db_;
    sqlite3_stmt* st_ = nullptr;
    std::unordered_map<std::string, int> cols_;
};

namespace {
CodeRow readCode(const auto& s) {
    CodeRow r;
    r.id = s.integer("id");
    r.code = s.text("code"); r.region = s.text("region"); r.firstSeen = s.text("first_seen");
    r.lastSeen = s.text("last_seen"); r.expiresAt = s.text("expires_at"); r.status = s.text("status");
    r.confidence = static_cast<int>(s.integer("confidence")); r.sourceCount = static_cast<int>(s.integer("source_count"));
    r.validationNotes = s.text("validation_notes"); r.notes = s.text("notes"); r.fresh = s.integer("fresh") != 0;
    r.checkVerdict = s.text("check_verdict"); r.checkScore = static_cast<int>(s.integer("check_score", -1));
    r.checkNotes = s.text("check_notes"); r.checkedAt = s.text("checked_at"); r.discordState = s.text("discord_state");
    r.game = s.text("game"); r.platform = s.text("platform"); r.kind = s.text("kind"); r.snippet = s.text("snippet");
    return r;
}
SourceRow readSource(const auto& s) {
    SourceRow r;
    r.id = s.integer("id"); r.name = s.text("name"); r.url = s.text("url"); r.type = s.text("type");
    r.trust = s.text("trust"); r.baseScore = static_cast<int>(s.integer("base_score"));
    r.lastChecked = s.text("last_checked"); r.lastStatus = s.text("last_status"); r.lastStatusNull = s.isNull("last_status");
    r.okCount = static_cast<int>(s.integer("ok_count")); r.errorCount = static_cast<int>(s.integer("error_count"));
    r.codesFound = static_cast<int>(s.integer("codes_found")); r.etag = s.text("etag");
    r.lastModified = s.text("last_modified"); r.lastContentAt = s.text("last_content_at");
    return r;
}
}  // namespace

Store::Store(const std::string& path) {
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK)
        throw std::runtime_error("Can't open database " + path + ": " + (db_ ? sqlite3_errmsg(db_) : "?"));
    sqlite3_busy_timeout(db_, 30000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    migrate();
}
Store::~Store() { if (db_) sqlite3_close(db_); }

void Store::exec(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error("SQL: " + msg);
    }
}

int Store::schemaVersion() {
    std::lock_guard lock(mu_);
    Stmt s(db_, "PRAGMA user_version");
    return s.step() ? static_cast<int>(s.integer("user_version")) : 0;
}

void Store::migrate() {
    std::lock_guard lock(mu_);
    int current = schemaVersion();
    int n = static_cast<int>(sizeof(MIGRATIONS) / sizeof(MIGRATIONS[0]));
    for (int v = current + 1; v <= n; ++v) {
        exec("BEGIN");
        try {
            exec(MIGRATIONS[v - 1]);
            exec("PRAGMA user_version=" + std::to_string(v));
            exec("COMMIT");
        } catch (...) {
            exec("ROLLBACK");
            throw;
        }
    }
}

// ── sources ─────────────────────────────────────────────────────
SourceRow Store::registerSource(const std::string& name, const std::string& url, const std::string& type,
                                const std::string& trust, int score) {
    std::lock_guard lock(mu_);
    Stmt(db_, R"(INSERT INTO sources(name,url,type,trust,base_score) VALUES(?,?,?,?,?)
        ON CONFLICT(url) DO UPDATE SET name=excluded.name, type=excluded.type, trust=excluded.trust,
        base_score=excluded.base_score)")
        .bind(1, name).bind(2, url).bind(3, type.empty() ? "web" : type).bind(4, trust.empty() ? "unverified" : trust)
        .bind(5, score).step();
    return *getSource(url);
}
std::optional<SourceRow> Store::getSource(const std::string& url) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT * FROM sources WHERE url=?");
    s.bind(1, url);
    if (s.step()) return readSource(s);
    return std::nullopt;
}
void Store::updateSourceHealth(const std::string& url, bool ok, const std::string& status, const std::string& etag,
                               const std::string& lastModified, int codesFound) {
    std::lock_guard lock(mu_);
    Stmt(db_, R"(UPDATE sources SET last_checked=?, last_status=?, ok_count=ok_count+?, error_count=error_count+?,
        codes_found=codes_found+?, etag=COALESCE(?,etag), last_modified=COALESCE(?,last_modified) WHERE url=?)")
        .bind(1, utcNowIso()).bind(2, status).bind(3, ok ? 1 : 0).bind(4, ok ? 0 : 1).bind(5, codesFound)
        .bindOpt(6, etag).bindOpt(7, lastModified).bind(8, url).step();
}
std::vector<SourceRow> Store::listSources() {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT * FROM sources ORDER BY base_score DESC, name");
    std::vector<SourceRow> out;
    while (s.step()) out.push_back(readSource(s));
    return out;
}
void Store::setContentTime(const std::string& url) {
    std::lock_guard lock(mu_);
    Stmt(db_, "UPDATE sources SET last_content_at=? WHERE url=?").bind(1, utcNowIso()).bind(2, url).step();
}

// ── codes ───────────────────────────────────────────────────────
std::vector<CodeRow> Store::queryCodes(const std::string& sql, const std::vector<std::string>& args, bool) {
    std::lock_guard lock(mu_);
    Stmt s(db_, sql);
    for (size_t i = 0; i < args.size(); ++i) s.bind(static_cast<int>(i + 1), args[i]);
    std::vector<CodeRow> out;
    while (s.step()) out.push_back(readCode(s));
    return out;
}
std::optional<CodeRow> Store::getCodeByText(const std::string& code) {
    auto r = queryCodes("SELECT * FROM codes WHERE code_key=? ORDER BY id LIMIT 1", {codeKey(code)});
    if (r.empty()) return std::nullopt;
    return r.front();
}
std::optional<CodeRow> Store::getCode(long long id) {
    auto r = queryCodes("SELECT * FROM codes WHERE id=?", {std::to_string(id)});
    if (r.empty()) return std::nullopt;
    return r.front();
}
long long Store::insertCode(const std::string& code, const std::string& region, const std::string& expiresAt,
                            const std::string& status, int confidence, const std::string& notes, const std::string& game,
                            const std::string& platform, const std::string& kind) {
    std::lock_guard lock(mu_);
    std::string now = utcNowIso();
    Stmt(db_, R"(INSERT INTO codes(code,code_key,region,first_seen,last_seen,expires_at,status,confidence,
        validation_notes,game,platform,kind) VALUES(?,?,?,?,?,?,?,?,?,?,?,?))")
        .bind(1, code).bind(2, codeKey(code)).bind(3, region).bind(4, now).bind(5, now).bindOpt(6, expiresAt)
        .bind(7, status).bind(8, confidence).bind(9, notes).bind(10, game).bind(11, platform).bind(12, kind).step();
    return sqlite3_last_insert_rowid(db_);
}
void Store::refineCode(long long id, const std::string& game, const std::string& platform, const std::string& kind) {
    std::lock_guard lock(mu_);
    if (!game.empty() && game != "other")
        Stmt(db_, "UPDATE codes SET game=? WHERE id=? AND game='other'").bind(1, game).bind(2, id).step();
    if (!platform.empty() && platform != "All platforms")
        Stmt(db_, "UPDATE codes SET platform=? WHERE id=? AND platform IN ('','All platforms')").bind(1, platform).bind(2, id).step();
    if (!kind.empty() && kind != "promo")
        Stmt(db_, "UPDATE codes SET kind=? WHERE id=? AND kind='promo'").bind(1, kind).bind(2, id).step();
}
void Store::touchCode(long long id, const std::string& region, const std::string& expiresAt) {
    std::lock_guard lock(mu_);
    Stmt(db_, R"(UPDATE codes SET last_seen=?,
        region=CASE WHEN region='Unknown' THEN ? WHEN region!=? AND ?!='Unknown' THEN 'Both' ELSE region END,
        expires_at=COALESCE(expires_at,?) WHERE id=?)")
        .bind(1, utcNowIso()).bind(2, region).bind(3, region).bind(4, region).bindOpt(5, expiresAt).bind(6, id).step();
}
void Store::addSighting(long long codeId, long long sourceId, const std::string& documentUrl, const std::string& snippet,
                        const std::string& report) {
    std::lock_guard lock(mu_);
    Stmt(db_, R"(INSERT INTO sightings(code_id,source_id,document_url,seen_at,snippet,report) VALUES(?,?,?,?,?,?)
        ON CONFLICT(code_id,source_id,document_url) DO UPDATE SET seen_at=excluded.seen_at,
        snippet=excluded.snippet, report=excluded.report)")
        .bind(1, codeId).bind(2, sourceId).bind(3, documentUrl).bind(4, utcNowIso()).bind(5, truncateUtf8(snippet, 600))
        .bind(6, report).step();
    Stmt(db_, "UPDATE codes SET source_count=(SELECT COUNT(DISTINCT source_id) FROM sightings WHERE code_id=?) WHERE id=?")
        .bind(1, codeId).bind(2, codeId).step();
}
ReportCounts Store::reportCounts(long long codeId) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT report, COUNT(DISTINCT source_id) n FROM sightings WHERE code_id=? GROUP BY report");
    s.bind(1, codeId);
    ReportCounts c;
    while (s.step()) {
        std::string r = s.text("report");
        int n = static_cast<int>(s.integer("n"));
        if (r == "active") c.active = n;
        else if (r == "expired") c.expired = n;
        else if (r == "exhausted") c.exhausted = n;
        else if (r == "removed") c.removed = n;
    }
    return c;
}
int Store::activeSightingsOn(long long sourceId, const std::string& documentUrl) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT COUNT(*) n FROM sightings WHERE source_id=? AND document_url=? AND report='active'");
    s.bind(1, sourceId).bind(2, documentUrl);
    return s.step() ? static_cast<int>(s.integer("n")) : 0;
}
std::vector<long long> Store::markRemoved(long long sourceId, const std::string& documentUrl,
                                          const std::vector<std::string>& presentCodes) {
    std::lock_guard lock(mu_);
    std::set<std::string> present(presentCodes.begin(), presentCodes.end());
    Stmt s(db_, R"(SELECT g.id, g.code_id, k.code FROM sightings g JOIN codes k ON k.id=g.code_id
        WHERE g.source_id=? AND g.document_url=? AND g.report='active')");
    s.bind(1, sourceId).bind(2, documentUrl);
    std::vector<std::pair<long long, long long>> gone;
    while (s.step())
        if (!present.count(s.text("code"))) gone.emplace_back(s.integer("id"), s.integer("code_id"));
    std::vector<long long> ids;
    for (auto& [sid, cid] : gone) {
        Stmt(db_, "UPDATE sightings SET report='removed' WHERE id=?").bind(1, sid).step();
        ids.push_back(cid);
    }
    return ids;
}
std::vector<std::string> Store::allSnippets(long long codeId) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT snippet FROM sightings WHERE code_id=? ORDER BY report='active' DESC, seen_at DESC");
    s.bind(1, codeId);
    std::vector<std::string> out;
    while (s.step()) out.push_back(s.text("snippet"));
    return out;
}
void Store::setDiscordState(const std::vector<long long>& ids, const std::string& state) {
    std::lock_guard lock(mu_);
    for (auto id : ids)
        Stmt(db_, "UPDATE codes SET discord_state=?, discord_sent_at=? WHERE id=?").bind(1, state).bind(2, utcNowIso()).bind(3, id).step();
}
void Store::saveCheck(long long id, const std::string& verdict, int score, const std::string& notes) {
    std::lock_guard lock(mu_);
    Stmt(db_, "UPDATE codes SET check_verdict=?, check_score=?, check_notes=?, checked_at=? WHERE id=?")
        .bind(1, verdict).bind(2, score).bind(3, truncateUtf8(notes, 1000)).bind(4, utcNowIso()).bind(5, id).step();
}
std::map<long long, std::pair<int, int>> Store::sourceTrackRecord() {
    std::lock_guard lock(mu_);
    Stmt s(db_, R"(SELECT g.source_id sid,
          SUM(CASE WHEN c.status IN ('SUCCESS','ALREADY_REDEEMED') THEN 1 ELSE 0 END) worked,
          SUM(CASE WHEN c.status IN ('EXPIRED','INVALID','LIMIT_REACHED') THEN 1 ELSE 0 END) failed
        FROM sightings g JOIN codes c ON c.id=g.code_id
        WHERE g.report='active' AND g.document_url != 'manual://entry'
          AND c.status IN ('SUCCESS','ALREADY_REDEEMED','EXPIRED','INVALID','LIMIT_REACHED')
        GROUP BY g.source_id)");
    std::map<long long, std::pair<int, int>> out;
    while (s.step()) out[s.integer("sid")] = {static_cast<int>(s.integer("worked")), static_cast<int>(s.integer("failed"))};
    return out;
}
std::vector<std::pair<long long, int>> Store::sightingSources(long long codeId) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT DISTINCT s.id, s.base_score FROM sightings g JOIN sources s ON s.id=g.source_id WHERE g.code_id=?");
    s.bind(1, codeId);
    std::vector<std::pair<long long, int>> out;
    while (s.step()) out.emplace_back(s.integer("id"), static_cast<int>(s.integer("base_score")));
    return out;
}
std::vector<Sighting> Store::sightingsFor(long long codeId) {
    std::lock_guard lock(mu_);
    Stmt s(db_, R"(SELECT g.*, s.name AS source_name, s.trust FROM sightings g
        LEFT JOIN sources s ON s.id=g.source_id WHERE g.code_id=? ORDER BY g.seen_at DESC)");
    s.bind(1, codeId);
    std::vector<Sighting> out;
    while (s.step()) {
        Sighting g;
        g.id = s.integer("id"); g.codeId = s.integer("code_id"); g.sourceId = s.integer("source_id");
        g.documentUrl = s.text("document_url"); g.seenAt = s.text("seen_at"); g.snippet = s.text("snippet");
        g.report = s.text("report"); g.sourceName = s.text("source_name"); g.trust = s.text("trust");
        out.push_back(g);
    }
    return out;
}
void Store::updateCode(long long id, const std::map<std::string, std::string>& textFields,
                       const std::map<std::string, long long>& intFields) {
    if (textFields.empty() && intFields.empty()) return;
    std::string cols;
    for (auto& [k, v] : textFields) {
        if (!updatableColumns().count(k)) throw std::invalid_argument("not an editable column: " + k);
        cols += (cols.empty() ? "" : ", ") + k + "=?";
    }
    for (auto& [k, v] : intFields) {
        if (!updatableColumns().count(k)) throw std::invalid_argument("not an editable column: " + k);
        cols += (cols.empty() ? "" : ", ") + k + "=?";
    }
    std::lock_guard lock(mu_);
    Stmt s(db_, "UPDATE codes SET " + cols + " WHERE id=?");
    int i = 1;
    for (auto& [k, v] : textFields) s.bind(i++, v);
    for (auto& [k, v] : intFields) s.bind(i++, v);
    s.bind(i, id).step();
}
void Store::setStatus(long long id, const std::string& status, const std::string& notes) {
    std::map<std::string, std::string> f{{"status", status}};
    if (!notes.empty()) f["notes"] = notes;
    updateCode(id, f);
}
std::vector<CodeRow> Store::listCodes(int limit) {
    return queryCodes("SELECT * FROM codes ORDER BY first_seen DESC LIMIT " + std::to_string(limit), {});
}
const std::vector<std::string>& Store::viewNames() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> n;
        for (auto& [k, w] : viewFilters()) n.push_back(k);
        return n;
    }();
    return names;
}
std::vector<CodeRow> Store::codesView(const std::string& view, const std::string& searchText, const std::string& game) {
    std::string q = R"(SELECT c.*, (SELECT g.snippet FROM sightings g WHERE g.code_id=c.id
            ORDER BY g.report='active' DESC, g.seen_at DESC LIMIT 1) AS snippet FROM codes c WHERE )" + filterFor(view);
    std::vector<std::string> args;
    if (!game.empty()) { q += " AND c.game=?"; args.push_back(game); }
    if (!searchText.empty()) { q += " AND c.code LIKE ?"; args.push_back("%" + upper(searchText) + "%"); }
    q += " ORDER BY c.fresh DESC, c.confidence DESC, c.first_seen DESC LIMIT 2000";
    return queryCodes(q, args);
}
std::map<std::string, int> Store::viewCounts(const std::string& game) {
    std::lock_guard lock(mu_);
    std::map<std::string, int> out;
    for (auto& [k, w] : viewFilters()) {
        Stmt s(db_, "SELECT COUNT(*) n FROM codes WHERE (" + w + ")" + (game.empty() ? "" : " AND game=?"));
        if (!game.empty()) s.bind(1, game);
        out[k] = s.step() ? static_cast<int>(s.integer("n")) : 0;
    }
    return out;
}
std::map<std::string, std::pair<int, int>> Store::gameCounts() {
    std::lock_guard lock(mu_);
    Stmt s(db_, R"(SELECT game, SUM(CASE WHEN status='READY' THEN 1 ELSE 0 END) usable,
        SUM(CASE WHEN status NOT IN ('REJECTED','IGNORED') THEN 1 ELSE 0 END) total FROM codes GROUP BY game)");
    std::map<std::string, std::pair<int, int>> out;
    while (s.step()) out[s.text("game")] = {static_cast<int>(s.integer("usable")), static_cast<int>(s.integer("total"))};
    return out;
}
int Store::expirePastDue() {
    std::lock_guard lock(mu_);
    Stmt s(db_, R"(UPDATE codes SET status='EXPIRED', notes='Past known expiry date'
        WHERE expires_at IS NOT NULL AND expires_at < ? AND status IN ('DISCOVERED','VALIDATED','READY'))");
    s.bind(1, todayIso()).step();
    return s.changes();
}
std::map<std::string, int> Store::stats() {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT status, COUNT(*) n FROM codes GROUP BY status");
    std::map<std::string, int> out;
    while (s.step()) out[s.text("status")] = static_cast<int>(s.integer("n"));
    return out;
}

// ── attempts ────────────────────────────────────────────────────
long long Store::startAttempt(long long codeId, const std::string& uidHash, const std::string& region) {
    std::lock_guard lock(mu_);
    Stmt(db_, "INSERT INTO redemption_attempts(code_id,account_uid_hash,region,started_at) VALUES(?,?,?,?)")
        .bind(1, codeId).bind(2, uidHash).bind(3, region).bind(4, utcNowIso()).step();
    return sqlite3_last_insert_rowid(db_);
}
void Store::markSubmitted(long long attemptId) {
    std::lock_guard lock(mu_);
    Stmt(db_, "UPDATE redemption_attempts SET submitted_at=?, counted=1 WHERE id=? AND counted=0")
        .bind(1, utcNowIso()).bind(2, attemptId).step();
}
void Store::finishAttempt(long long attemptId, const std::string& outcome, const std::string& method, const std::string& evidence) {
    std::lock_guard lock(mu_);
    Stmt(db_, "UPDATE redemption_attempts SET outcome=?, verification_method=?, evidence=? WHERE id=?")
        .bind(1, outcome).bind(2, method).bind(3, truncateUtf8(evidence, 1500)).bind(4, attemptId).step();
}
int Store::countedAttemptsSince(const std::string& uidHash, const std::string& sinceIso) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT COUNT(*) n FROM redemption_attempts WHERE account_uid_hash=? AND counted=1 AND submitted_at>=?");
    s.bind(1, uidHash).bind(2, sinceIso);
    return s.step() ? static_cast<int>(s.integer("n")) : 0;
}
std::string Store::lastSubmission(const std::string& uidHash) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT submitted_at FROM redemption_attempts WHERE account_uid_hash=? AND counted=1 ORDER BY submitted_at DESC LIMIT 1");
    s.bind(1, uidHash);
    return s.step() ? s.text("submitted_at") : "";
}
std::vector<AttemptRow> Store::recentAttempts(int limit) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT a.*, c.code FROM redemption_attempts a JOIN codes c ON c.id=a.code_id ORDER BY a.id DESC LIMIT ?");
    s.bind(1, limit);
    std::vector<AttemptRow> out;
    while (s.step()) {
        AttemptRow a;
        a.id = s.integer("id"); a.codeId = s.integer("code_id"); a.code = s.text("code"); a.region = s.text("region");
        a.startedAt = s.text("started_at"); a.submittedAt = s.text("submitted_at"); a.outcome = s.text("outcome");
        a.method = s.text("verification_method"); a.evidence = s.text("evidence"); a.counted = s.integer("counted") != 0;
        out.push_back(a);
    }
    return out;
}

// ── stats / patches / runs ──────────────────────────────────────
namespace {
template <class S>
Json rowJson(const S& s, const std::vector<std::string>& textCols, const std::vector<std::string>& intCols) {
    Json o = Json::object();
    for (auto& c : textCols) o[c] = s.isNull(c) ? Json() : Json(s.text(c));
    for (auto& c : intCols) o[c] = s.isNull(c) ? Json() : Json(s.integer(c));
    return o;
}
}  // namespace

Json Store::weaponStat(const std::string& game, const std::string& key, const std::string& source) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT stats FROM weapon_stats WHERE game=? AND weapon_key=? AND source=?");
    s.bind(1, game).bind(2, key).bind(3, source);
    if (!s.step()) return Json();
    try { return Json::parse(s.text("stats")); } catch (...) { return Json(); }
}
void Store::putWeaponStat(const std::string& game, const std::string& weapon, const std::string& key, const std::string& cls,
                          const std::string& statsJson, const std::string& source, bool update) {
    std::lock_guard lock(mu_);
    if (update)
        Stmt(db_, "UPDATE weapon_stats SET stats=?, class=?, seen_at=? WHERE game=? AND weapon_key=? AND source=?")
            .bind(1, statsJson).bind(2, cls).bind(3, utcNowIso()).bind(4, game).bind(5, key).bind(6, source).step();
    else
        Stmt(db_, "INSERT INTO weapon_stats(game, weapon, weapon_key, class, stats, source, seen_at) VALUES(?,?,?,?,?,?,?)")
            .bind(1, game).bind(2, weapon).bind(3, key).bind(4, cls).bind(5, statsJson).bind(6, source).bind(7, utcNowIso()).step();
}
void Store::addStatChange(const std::string& game, const std::string& weapon, const std::string& field, const std::string& oldJson,
                          const std::string& newJson, const std::string& source) {
    std::lock_guard lock(mu_);
    Stmt(db_, "INSERT INTO stat_changes(game, weapon, field, old, new, source, seen_at) VALUES(?,?,?,?,?,?,?)")
        .bind(1, game).bind(2, weapon).bind(3, field).bind(4, oldJson).bind(5, newJson).bind(6, source).bind(7, utcNowIso()).step();
}
Json Store::statChanges(const std::string& game, const std::string& since, int limit) {
    std::lock_guard lock(mu_);
    Stmt s(db_, std::string("SELECT * FROM stat_changes WHERE game=?") + (since.empty() ? "" : " AND seen_at>?") +
                    " ORDER BY id DESC LIMIT ?");
    int i = 1;
    s.bind(i++, game);
    if (!since.empty()) s.bind(i++, since);
    s.bind(i, limit);
    Json out = Json::array();
    while (s.step()) out.push(rowJson(s, {"game", "weapon", "field", "old", "new", "source", "seen_at"}, {"id"}));
    return out;
}
Json Store::weaponStatsFor(const std::string& game) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT * FROM weapon_stats WHERE game=? ORDER BY weapon");
    s.bind(1, game);
    Json out = Json::array();
    while (s.step()) out.push(rowJson(s, {"game", "weapon", "weapon_key", "class", "stats", "source", "seen_at"}, {"id"}));
    return out;
}
bool Store::insertPatch(const std::string& game, const std::string& title, const std::string& url, const std::string& published,
                        const std::string& changesJson, bool balance) {
    std::lock_guard lock(mu_);
    Stmt s(db_, R"(INSERT OR IGNORE INTO patches(game, title, url, published, seen_at, weapon_changes, balance)
                   VALUES(?,?,?,?,?,?,?))");
    s.bind(1, game).bind(2, truncateUtf8(title, 200)).bind(3, url).bindOpt(4, published).bind(5, utcNowIso())
        .bind(6, changesJson).bind(7, balance ? 1 : 0).step();
    return s.changes() == 1;
}
Json Store::patch(const std::string& game, const std::string& url) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT * FROM patches WHERE game=? AND url=?");
    s.bind(1, game).bind(2, url);
    if (!s.step()) return Json();
    return rowJson(s, {"game", "title", "url", "published", "seen_at", "weapon_changes"}, {"id", "balance"});
}
void Store::updatePatchChanges(const std::string& game, const std::string& url, const std::string& changesJson, bool balance) {
    std::lock_guard lock(mu_);
    Stmt(db_, "UPDATE patches SET weapon_changes=?, balance=? WHERE game=? AND url=?")
        .bind(1, changesJson).bind(2, balance ? 1 : 0).bind(3, game).bind(4, url).step();
}
Json Store::patchesSince(const std::string& game, const std::string& sinceIso) {
    std::lock_guard lock(mu_);
    Stmt s(db_, R"(SELECT * FROM patches WHERE game=? AND COALESCE(published, seen_at) > ?
                   ORDER BY COALESCE(published, seen_at) DESC)");
    s.bind(1, game).bind(2, sinceIso);
    Json out = Json::array();
    while (s.step()) out.push(rowJson(s, {"game", "title", "url", "published", "seen_at", "weapon_changes"}, {"id", "balance"}));
    return out;
}
void Store::addBuildRun(const std::string& game, const std::string& weapon, const std::string& key, const std::string& mode,
                        int kills, int deaths, bool won, int placement, const std::string& notes) {
    std::lock_guard lock(mu_);
    Stmt st(db_, R"(INSERT INTO build_runs(game, weapon, build_key, mode, kills, deaths, won, placement, notes, played_at)
                    VALUES(?,?,?,?,?,?,?,?,?,?))");
    st.bind(1, game).bind(2, weapon).bind(3, key).bind(4, mode).bind(5, kills).bind(6, deaths).bind(7, won ? 1 : 0);
    if (placement > 0) st.bind(8, placement); else st.bindNull(8);
    st.bind(9, truncateUtf8(notes, 300)).bind(10, utcNowIso()).step();
}
Json Store::buildRuns(const std::string& game, const std::string& key, int limit) {
    std::lock_guard lock(mu_);
    std::string q = "SELECT * FROM build_runs WHERE 1=1";
    if (!game.empty()) q += " AND game=?";
    if (!key.empty()) q += " AND build_key=?";
    q += " ORDER BY id DESC LIMIT ?";
    Stmt s(db_, q);
    int i = 1;
    if (!game.empty()) s.bind(i++, game);
    if (!key.empty()) s.bind(i++, key);
    s.bind(i, limit);
    Json out = Json::array();
    while (s.step())
        out.push(rowJson(s, {"game", "weapon", "build_key", "mode", "notes", "played_at"}, {"id", "kills", "deaths", "won", "placement"}));
    return out;
}

// ── state & logs ────────────────────────────────────────────────
void Store::setState(const std::string& key, const std::string& value) {
    std::lock_guard lock(mu_);
    Stmt(db_, "INSERT INTO app_state(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value")
        .bind(1, key).bind(2, value).step();
}
std::string Store::getState(const std::string& key, const std::string& fallback) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT value FROM app_state WHERE key=?");
    s.bind(1, key);
    return s.step() ? s.text("value") : fallback;
}
void Store::logScan(const std::string& name, const std::string& status, const std::string& details) {
    std::lock_guard lock(mu_);
    Stmt(db_, "INSERT INTO scan_log(scanned_at,source_name,status,details) VALUES(?,?,?,?)")
        .bind(1, utcNowIso()).bind(2, name).bind(3, status).bind(4, truncateUtf8(details, 1000)).step();
}
std::vector<LogRow> Store::recentScans(int limit) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT * FROM scan_log ORDER BY id DESC LIMIT ?");
    s.bind(1, limit);
    std::vector<LogRow> out;
    while (s.step()) out.push_back({s.integer("id"), s.text("scanned_at"), s.text("source_name"), s.text("status"), s.text("details")});
    return out;
}
void Store::addAudit(const std::string& event, const std::string& details) {
    std::lock_guard lock(mu_);
    Stmt(db_, "INSERT INTO audit_log(ts,event,details) VALUES(?,?,?)")
        .bind(1, utcNowIso()).bind(2, event).bind(3, truncateUtf8(details, 2000)).step();
}
std::vector<LogRow> Store::recentAudit(int limit) {
    std::lock_guard lock(mu_);
    Stmt s(db_, "SELECT * FROM audit_log ORDER BY id DESC LIMIT ?");
    s.bind(1, limit);
    std::vector<LogRow> out;
    while (s.step()) out.push_back({s.integer("id"), s.text("ts"), "", s.text("event"), s.text("details")});
    return out;
}

}  // namespace hunter
