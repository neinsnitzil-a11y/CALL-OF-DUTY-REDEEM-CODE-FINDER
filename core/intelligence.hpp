// Extraction → validation → dedup → region → cross-source reports → confidence → status.
#pragma once
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "extraction.hpp"
#include "store.hpp"

namespace hunter {

struct Found {
    std::string code;
    bool isNew = false;
};

class Intelligence {
public:
    Intelligence(Config& cfg, Store& store) : cfg_(cfg), store_(store) {}

    // gameHint: the source's game id, or "" / "auto" to detect it from the page.
    std::vector<Found> processDocument(const Document& doc, const SourceRow& source, const std::string& gameHint = "");
    std::string reassess(long long codeId);
    int reassessAll();  // returns how many codes passed their expiry date
    // Returns (id, problems). id 0 = rejected.
    std::pair<long long, std::vector<std::string>> addManualCode(const std::string& code, const std::string& region,
                                                                 const std::string& note, const std::string& game);
    void forgetTrackRecord();
    static std::string pickGame(const std::string& sourceGame, const Candidate& c);

private:
    std::map<long long, std::pair<int, int>> trackRecord();
    bool manualEntry(long long codeId);
    Config& cfg_;
    Store& store_;
    std::mutex recMu_;
    std::chrono::steady_clock::time_point recAt_{};
    std::map<long long, std::pair<int, int>> rec_;
    bool recValid_ = false;
};

}  // namespace hunter
