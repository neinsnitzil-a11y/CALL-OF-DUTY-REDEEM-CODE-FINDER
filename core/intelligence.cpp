#include "intelligence.hpp"

#include <algorithm>
#include <set>

#include "games.hpp"
#include "util.hpp"
#include "validation.hpp"

namespace hunter {

namespace {
const std::set<std::string> kPreRedemption = {"DISCOVERED", "VALIDATED", "READY", "LIKELY_EXPIRED", "LIKELY_EXHAUSTED",
                                              "UNKNOWN", "REGION_MISMATCH"};
}

std::string Intelligence::pickGame(const std::string& sourceGame, const Candidate& c) {
    if (!sourceGame.empty() && sourceGame != "auto") return sourceGame;
    if (!c.gameHint.empty()) return c.gameHint;
    return isCodmShape(c.code) ? "codm" : "other";
}

std::vector<Found> Intelligence::processDocument(const Document& doc, const SourceRow& source, const std::string& gameHint) {
    // First read of a monitored page is the baseline: codes already there are recorded but don't alert.
    bool baseline = source.type != "search-result" && source.type != "manual" && source.okCount == 0 && source.lastStatusNull;
    Json cfg = cfg_.snapshot();
    std::vector<Found> results;
    for (const auto& cand : extractFromDocument(doc)) {
        std::string game = pickGame(gameHint, cand);
        bool valid = validateCode(cand.code, cfg).valid;
        auto existing = store_.getCodeByText(cand.code);
        if (!existing && !valid) continue;  // never store junk tokens
        long long id;
        bool isNew = false;
        std::string region = game == "codm" ? cand.region : "Unknown";
        if (!existing) {
            id = store_.insertCode(cand.code, region, cand.expiresAt.value_or(""), "DISCOVERED", 0, "", game,
                                   cand.platform, cand.kind);
            isNew = cand.report == "active" && !baseline;
            if (isNew) store_.addAudit("CODE_DISCOVERED", cand.code + " via " + source.name);
        } else {
            id = existing->id;
            store_.touchCode(id, existing->game == "codm" ? cand.region : "Unknown", cand.expiresAt.value_or(""));
            store_.refineCode(id, game, cand.platform, cand.kind);
        }
        store_.addSighting(id, source.id, doc.url, cand.snippet, cand.report);
        if (isNew) store_.updateCode(id, {}, {{"fresh", 1}});
        reassess(id);
        results.push_back({cand.code, isNew});
    }
    return results;
}

std::map<long long, std::pair<int, int>> Intelligence::trackRecord() {
    std::lock_guard lock(recMu_);
    auto now = std::chrono::steady_clock::now();
    if (!recValid_ || now - recAt_ > std::chrono::seconds(60)) {
        rec_ = store_.sourceTrackRecord();
        recAt_ = now;
        recValid_ = true;
    }
    return rec_;
}

void Intelligence::forgetTrackRecord() {
    std::lock_guard lock(recMu_);
    recValid_ = false;
}

bool Intelligence::manualEntry(long long codeId) {
    auto s = store_.sightingsFor(codeId);
    return std::any_of(s.begin(), s.end(), [](const Sighting& g) { return g.documentUrl == "manual://entry"; });
}

std::string Intelligence::reassess(long long codeId) {
    auto row = store_.getCode(codeId);
    if (!row) return "";
    Json cfg = cfg_.snapshot();
    auto record = trackRecord();
    std::vector<int> scores;
    for (auto& [sid, base] : store_.sightingSources(codeId)) {
        auto it = record.find(sid);
        scores.push_back(it == record.end() ? base : measuredScore(base, it->second.first, it->second.second));
    }
    bool shape = knownFormat(row->code, row->game);
    int confidence = codeConfidence(scores, row->region, row->expiresAt, cfg, shape);
    ReportCounts reports = store_.reportCounts(codeId);
    std::string status = row->status;
    Validation v = validateCode(row->code, cfg);
    std::string notes = !v.reasons.empty() ? join(v.reasons, "; ") : (shape ? "official code format" : "unusual format");
    notes += " · listed active by " + std::to_string(reports.active) + ", expired by " + std::to_string(reports.expired) +
             ", limit-reached by " + std::to_string(reports.exhausted) + ", removed by " + std::to_string(reports.removed) +
             " source(s)";

    if (kPreRedemption.count(status)) {
        long long threshold = jinteger(cfg.get("intelligence"), "ready_confidence_threshold", 60);
        auto regions = cfg_.accountRegions();
        bool regionOk = row->game != "codm" || row->region == "Unknown" || row->region == "Both" ||
                        std::find(regions.begin(), regions.end(), row->region) != regions.end();
        if (!v.valid) status = "REJECTED";
        else if (isExpired(row->expiresAt)) status = "EXPIRED";
        else if (reports.exhausted > 0) status = "LIKELY_EXHAUSTED";
        else if (reports.expired > 0) status = "LIKELY_EXPIRED";
        else if (reports.active == 0 && reports.removed > 0) status = "LIKELY_EXPIRED";
        else if (status == "UNKNOWN") {}  // tried before with no clear answer: leave for the user
        else if (!regionOk) status = "REGION_MISMATCH";
        else if (!shape && scores.size() < 2 && !manualEntry(codeId)) status = "VALIDATED";
        else if (confidence >= threshold || (shape && scores.size() >= 2)) status = "READY";
        else status = "VALIDATED";
    }
    store_.updateCode(codeId, {{"status", status}, {"validation_notes", notes}}, {{"confidence", confidence}});
    return status;
}

int Intelligence::reassessAll() {
    int expired = store_.expirePastDue();
    for (const auto& row : store_.listCodes(10000))
        if (!isFinalStatus(row.status)) reassess(row.id);
    return expired;
}

std::pair<long long, std::vector<std::string>> Intelligence::addManualCode(const std::string& raw, const std::string& region,
                                                                          const std::string& note, const std::string& game) {
    std::string code = upper(trim(raw));
    Json cfg = cfg_.snapshot();
    Validation v = validateCode(code, cfg);
    if (!v.valid) return {0, v.reasons};
    if (auto existing = store_.getCodeByText(code)) return {existing->id, {"already in database"}};
    SourceRow src = store_.registerSource("Manual entry", "manual://entry", "manual", "official", 100);
    long long id = store_.insertCode(code, game == "codm" ? region : "Unknown", "", "DISCOVERED", 0, "", game,
                                     game != "codm" ? detectPlatform(code, {}) : "");
    store_.updateCode(id, {}, {{"fresh", 1}});
    store_.addSighting(id, src.id, "manual://entry", note.empty() ? "Added manually" : note);
    reassess(id);
    store_.addAudit("CODE_ADDED_MANUALLY", code);
    return {id, {}};
}

}  // namespace hunter
