// Account protection: one bound UID, persistent daily cap + cooldown, and a circuit breaker that
// stops all redemption on 403 / 429 / repeated failures / Emergency stop. Everything lives in
// SQLite so restarting the app never resets it.
#pragma once
#include <atomic>
#include <string>
#include <utility>

#include "config.hpp"
#include "store.hpp"

namespace hunter {

using Check = std::pair<bool, std::string>;

std::string uidHash(const std::string& uid);
std::string maskUid(const std::string& uid);
bool validUid(const std::string& uid);

class AccountBinding {
public:
    AccountBinding(Config& cfg, Store& store) : cfg_(cfg), store_(store) {}
    std::string uid() const { return cfg_.env("CODM_UID"); }
    std::string hash() const { return uid().empty() ? "" : uidHash(uid()); }
    bool configured() const { return !uid().empty(); }
    std::string masked() const { return configured() ? maskUid(uid()) : "(not set)"; }
    Check check();   // binds automatically on the first valid run
    bool rebind();

private:
    Config& cfg_;
    Store& store_;
};

class RateLimiter {
public:
    RateLimiter(Config& cfg, Store& store, AccountBinding& b) : cfg_(cfg), store_(store), binding_(b) {}
    int dailyCap() const;
    int cooldownMinutes() const;
    int usedToday();
    double cooldownRemaining();
    Check check();
    bool acquire();
    void release();

private:
    Config& cfg_;
    Store& store_;
    AccountBinding& binding_;
    static std::atomic<bool> busy_;
};

class CircuitBreaker {
public:
    CircuitBreaker(Config& cfg, Store& store) : cfg_(cfg), store_(store) {}
    struct State { bool open = false; std::string reason, until; };
    State state();
    Check check();
    void trip(const std::string& reason, int minutes = 0);
    void close(const std::string& note = "Cleared manually", bool automatic = false);
    void recordHttp(int status, const std::string& retryAfter = "");
    void recordOutcome(const std::string& outcome);

private:
    Config& cfg_;
    Store& store_;
};

class Protection {
public:
    Protection(Config& cfg, Store& store) : binding(cfg, store), limiter(cfg, store, binding), breaker(cfg, store) {}
    AccountBinding binding;
    RateLimiter limiter;
    CircuitBreaker breaker;
    Check checkAllowed();  // everything that must be true before opening a redemption
};

}  // namespace hunter
