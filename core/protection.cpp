#include "protection.hpp"

#include <cmath>
#include <cstdio>
#include <regex>
#include <set>

#include "util.hpp"

namespace hunter {

std::atomic<bool> RateLimiter::busy_{false};

std::string uidHash(const std::string& uid) { return sha256Hex(uid); }

std::string maskUid(const std::string& uid) {
    const std::string dot = "\xE2\x80\xA2";  // •
    std::string out;
    if (uid.size() <= 8) {
        for (size_t i = 0; i < uid.size(); ++i) out += dot;
        return out;
    }
    for (size_t i = 0; i < uid.size() - 8; ++i) out += dot;
    return uid.substr(0, 4) + out + uid.substr(uid.size() - 4);
}

bool validUid(const std::string& uid) {
    static const std::regex rx = re(R"(^\d{6,25}$)");
    return std::regex_match(uid, rx);
}

Check AccountBinding::check() {
    std::string u = uid();
    if (u.empty()) return {false, "CODM_UID is not set (Settings → Account)."};
    if (!validUid(u)) return {false, "CODM_UID must be digits only (your in-game Player UID)."};
    std::string bound = store_.getState("bound_uid_hash");
    if (bound.empty()) {
        store_.setState("bound_uid_hash", hash());
        store_.addAudit("ACCOUNT_BOUND", masked());
        return {true, "Bound to UID " + masked() + "."};
    }
    if (bound != hash())
        return {false, "The UID in settings differs from the bound account. Use Account → Re-bind if you really changed accounts."};
    return {true, "Bound to UID " + masked() + "."};
}

bool AccountBinding::rebind() {
    if (!validUid(uid())) return false;
    store_.setState("bound_uid_hash", hash());
    store_.addAudit("ACCOUNT_REBOUND", masked());
    return true;
}

int RateLimiter::dailyCap() const { return static_cast<int>(jinteger(cfg_.snapshot().get("account_protection"), "max_attempts_per_day", 5)); }
int RateLimiter::cooldownMinutes() const {
    return static_cast<int>(jinteger(cfg_.snapshot().get("account_protection"), "minimum_minutes_between_attempts", 15));
}
int RateLimiter::usedToday() { return store_.countedAttemptsSince(binding_.hash(), todayIso() + "T00:00:00+00:00"); }
double RateLimiter::cooldownRemaining() {
    auto last = parseIso(store_.lastSubmission(binding_.hash()));
    if (!last) return 0;
    double elapsed = (static_cast<double>(nowSeconds()) - static_cast<double>(*last)) / 60.0;
    return std::max(0.0, std::round((cooldownMinutes() - elapsed) * 10) / 10);
}
Check RateLimiter::check() {
    if (usedToday() >= dailyCap())
        return {false, "Daily cap reached (" + std::to_string(dailyCap()) + " submissions, resets 00:00 UTC)."};
    double rem = cooldownRemaining();
    if (rem > 0) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "Cooldown active: %.0f min until next submission.", rem);
        return {false, buf};
    }
    if (busy_.load()) return {false, "Another redemption is already in progress."};
    return {true, "Rate limits OK."};
}
bool RateLimiter::acquire() {
    bool expected = false;
    return busy_.compare_exchange_strong(expected, true);
}
void RateLimiter::release() { busy_.store(false); }

CircuitBreaker::State CircuitBreaker::state() {
    State s{store_.getState("cb_open", "0") == "1", store_.getState("cb_reason"), store_.getState("cb_until")};
    if (s.open && !s.until.empty() && s.until <= utcNowIso()) {
        close("Retry-After window elapsed", true);
        return {};
    }
    return s;
}
Check CircuitBreaker::check() {
    State s = state();
    if (!s.open) return {true, "Circuit closed."};
    std::string msg = "Redemption paused: " + s.reason;
    msg += s.until.empty() ? " — review, then clear it on the Account page." : " (until " + s.until + " UTC)";
    return {false, msg};
}
void CircuitBreaker::trip(const std::string& reason, int minutes) {
    store_.setState("cb_open", "1");
    store_.setState("cb_reason", reason);
    std::string until = minutes > 0 ? utcIso(static_cast<std::time_t>(nowSeconds() + minutes * 60LL)) : "";
    store_.setState("cb_until", until);
    store_.addAudit("CIRCUIT_OPEN", reason + " until=" + (until.empty() ? "manual" : until));
}
void CircuitBreaker::close(const std::string& note, bool automatic) {
    store_.setState("cb_open", "0");
    store_.setState("cb_reason", "");
    store_.setState("cb_until", "");
    store_.setState("consecutive_failures", "0");
    store_.addAudit(automatic ? "CIRCUIT_CLOSED_AUTO" : "CIRCUIT_CLOSED", note);
}
void CircuitBreaker::recordHttp(int status, const std::string& retryAfter) {
    Json c = cfg_.snapshot().get("account_protection");
    if (status == 403) trip("HTTP 403 from redemption site — access restricted");
    else if (status == 429) {
        int minutes = static_cast<int>(jinteger(c, "default_retry_after_minutes", 60));
        try { if (!retryAfter.empty()) minutes = std::max(1, std::stoi(retryAfter) / 60 + 1); } catch (...) {}
        trip("HTTP 429 from redemption site — rate limited", minutes);
    }
}
void CircuitBreaker::recordOutcome(const std::string& outcome) {
    Json c = cfg_.snapshot().get("account_protection");
    if (outcome == "BLOCKED") { trip("Site reported an access restriction"); return; }
    if (outcome == "RATE_LIMITED") { trip("Site reported too many requests", static_cast<int>(jinteger(c, "default_retry_after_minutes", 60))); return; }
    static const std::set<std::string> failures = {"UNKNOWN", "RATE_LIMITED", "BLOCKED", "ERROR"};
    int n = 0;
    try { n = std::stoi(store_.getState("consecutive_failures", "0")); } catch (...) {}
    n = failures.count(outcome) ? n + 1 : 0;
    store_.setState("consecutive_failures", std::to_string(n));
    if (n >= jinteger(c, "max_consecutive_failures", 3)) trip(std::to_string(n) + " consecutive unverified/failed redemptions");
}

Check Protection::checkAllowed() {
    auto b = binding.check();
    if (!b.first) return b;
    auto cb = breaker.check();
    if (!cb.first) return cb;
    return limiter.check();
}

}  // namespace hunter
