#include "discord.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <regex>
#include <set>
#include <thread>

#include "checker.hpp"
#include "games.hpp"
#include "util.hpp"

namespace hunter {

namespace {
constexpr int GREEN = 0x2ECC71, YELLOW = 0xF1C40F, RED = 0xE74C3C, BLUE = 0x3498DB, GREY = 0x95A5A6;
constexpr size_t MAX_EMBEDS = 10;

std::string relative(const std::string& iso) {
    auto t = parseIso(iso);
    if (!t) return "\xE2\x80\x94";
    double mins = (static_cast<double>(nowSeconds()) - static_cast<double>(*t)) / 60.0;
    if (mins < 60) return std::to_string(std::max(1, static_cast<int>(mins))) + " min ago";
    if (mins < 48 * 60) return std::to_string(static_cast<int>(mins / 60)) + " h ago";
    return std::to_string(static_cast<int>(mins / 1440)) + " days ago";
}
std::string regionText(const std::string& r) {
    if (r == "Global") return "🌍 Global";
    if (r == "Garena") return "🌏 Garena — redeem in game";
    if (r == "Both") return "🌍 Global + 🌏 Garena";
    if (r == "Unknown") return "🌍 Global (probably)";
    return r;
}
Json field(const std::string& name, const std::string& value, bool inl) {
    Json f = Json::object();
    f["name"] = name;
    f["value"] = truncateUtf8(value.empty() ? "\xE2\x80\x94" : value, 1024);
    f["inline"] = inl;
    return f;
}
}  // namespace

bool validWebhook(const std::string& url) {
    static const std::regex rx = re(R"(^https://(?:ptb\.|canary\.)?discord(?:app)?\.com/api/webhooks/\d+/[\w-]+/?$)");
    return std::regex_match(trim(url), rx);
}

std::string statusText(const CodeRow& row) {
    std::string pct = row.checkScore >= 0 ? " (" + std::to_string(row.checkScore) + "% chance)" : "";
    static const std::map<std::string, std::string> m = {
        {"LIKELY WORKS", "✅ Likely works"}, {"UNCERTAIN", "❔ Uncertain"}, {"LIKELY DEAD · OLD", "⚠ Probably used up (old)"},
        {"LIKELY DEAD · REMOVED", "⚠ Likely expired — sites took it down"},
        {"LIKELY DEAD · EXPIRED", "⚠ Likely expired — a site lists it as expired"},
        {"LIKELY DEAD · USED UP", "⚠ Likely used up — a site says the claim limit was reached"},
        {"DEAD · EXPIRED", "❌ Expired"}, {"DEAD · CLAIMS USED UP", "❌ Used up — claim limit reached (official)"},
        {"DEAD · INVALID", "❌ Invalid (official)"}, {"REDEEMED BY YOU", "🎁 You redeemed it"},
        {"CLAIMED BY YOU", "🎁 Already claimed by you"}};
    auto it = m.find(row.checkVerdict);
    if (it == m.end()) return row.checkVerdict.empty() ? "Not checked yet" : row.checkVerdict;
    bool withPct = row.checkVerdict == "LIKELY WORKS" || row.checkVerdict == "UNCERTAIN" || row.checkVerdict == "LIKELY DEAD · OLD";
    return it->second + (withPct ? pct : "");
}

int colorFor(const CodeRow& row) {
    const std::string& v = row.checkVerdict;
    if (v == "LIKELY WORKS") return GREEN;
    if (v == "UNCERTAIN") return YELLOW;
    if (v.find("BY YOU") != std::string::npos) return BLUE;
    return isDeadVerdict(v) ? RED : GREY;
}

std::string webhookFor(const Config& cfg, const std::string& gameId, const std::string& purpose) {
    std::string gid = upper(gameId.empty() ? "other" : gameId);
    if (purpose == "system") return cfg.env("DISCORD_WEBHOOK_SYSTEM");
    std::vector<std::string> keys;
    if (purpose == "updates") keys = {"DISCORD_UPDATES_WEBHOOK_" + gid, "DISCORD_UPDATES_WEBHOOK"};
    keys.push_back("DISCORD_WEBHOOK_" + gid);
    keys.push_back("DISCORD_WEBHOOK_URL");
    for (auto& k : keys) if (!cfg.env(k).empty()) return cfg.env(k);
    return "";
}

Json codeEmbed(const Config& cfg, const std::vector<std::string>& snippets, const CodeRow& row, const std::string& kind) {
    std::string reward = bestReward(snippets, row.code);
    if (reward.empty()) reward = "Not listed";
    const Game& g = gameById(row.game);
    std::string prefix = kind == "new" ? "🆕 New " + g.label() + " code" : kind == "list" ? "🎯" : "⚠ Update";
    if (kind == "update") prefix = startsWith(row.checkVerdict, "DEAD") ? "❌ Now dead" : "⚠ Update";
    Json fields = Json::array();
    fields.push(field("🎮 Game", g.name, true));
    fields.push(field("🎁 Reward", reward, true));
    if (row.game == "codm") fields.push(field("Region", regionText(row.region), true));
    else fields.push(field("Platform", row.platform.empty() ? "All platforms" : row.platform, true));
    fields.push(field("Status", statusText(row), false));
    fields.push(field("Seen on", std::to_string(row.sourceCount) + " site" + (row.sourceCount != 1 ? "s" : ""), true));
    fields.push(field("First seen", relative(row.firstSeen), true));
    if (!row.expiresAt.empty()) fields.push(field("Expires", row.expiresAt, true));
    std::string firstReason = split(row.checkNotes, '.').empty() ? "" : trim(split(row.checkNotes, '.').front());
    Json e = Json::object();
    e["title"] = truncateUtf8(kind == "list" ? "🎯 " + g.label() + ": " + row.code : prefix + ": " + row.code, 256);
    e["description"] = "```" + row.code + "```" +
                       (kind == "update" && !firstReason.empty() ? "\n" + truncateUtf8(firstReason, 300) + "." : "");
    e["color"] = colorFor(row);
    std::string url;
    if (row.game == "codm") url = row.region != "Garena" ? cfg.redemptionUrl(row.region) : "";
    else {
        auto [u, how] = redeemInfo(row.game, row.code, row.kind);
        url = u;
        fields.push(field("How to redeem", how, false));
    }
    e["fields"] = fields;
    Json footer = Json::object();
    footer["text"] = "COD Redemption Hunter · 'chance' is an estimate — the official redemption page decides";
    e["footer"] = footer;
    e["timestamp"] = utcNowIso();
    if (startsWith(url, "https://") && !isDeadVerdict(row.checkVerdict)) e["url"] = url;
    return e;
}

// ── notifier ────────────────────────────────────────────────────
Json DiscordNotifier::opts() const {
    Json o = Json::parse(R"({"enabled":true,"post_new":true,"new_codes":"all","post_updates":true,
        "post_list_on_connect":true,"list_max":25,"include_garena":true,"mention":"","username":"COD Redemption Hunter"})");
    const Json cfg = cfg_.snapshot();
    for (const auto& [k, v] : cfg.get("discord").items()) o[k] = v;
    return o;
}

std::string DiscordNotifier::route(const std::string& game, const std::string& purpose) const {
    return trim(override_.empty() ? webhookFor(cfg_, game, purpose) : override_);
}

Json DiscordNotifier::message(const std::string& content, const Json& embeds) const {
    Json o = opts();
    std::string mention = trim(jstr(o, "mention"));
    Json allowed = Json::object();
    allowed["parse"] = Json::array();
    if (mention == "@everyone" || mention == "@here") {
        allowed["parse"].push("everyone");
    } else if (!mention.empty() && std::all_of(mention.begin(), mention.end(), ::isdigit)) {
        Json roles = Json::array();
        roles.push(mention);
        allowed["roles"] = roles;
        mention = "<@&" + mention + ">";
    } else {
        mention.clear();
    }
    Json m = Json::object();
    m["username"] = truncateUtf8(jstr(o, "username", "COD Redemption Hunter"), 80);
    m["content"] = truncateUtf8(trim(mention + " " + content), 2000);
    m["embeds"] = embeds;
    m["allowed_mentions"] = allowed;
    return m;
}

void DiscordNotifier::wait(double s) {
    if (sleeper) sleeper(s);
    else std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(s * 1000)));
}

std::pair<bool, std::string> DiscordNotifier::post(const std::string& url, const Json& payload, int tries) {
    std::string last = "unknown error";
    for (int attempt = 0; attempt < tries; ++attempt) {
        HttpRequest req;
        req.method = "POST";
        req.url = url + (url.find('?') == std::string::npos ? "?wait=true" : "&wait=true");
        req.body = payload.dump();
        req.contentType = "application/json";
        req.headers["User-Agent"] = "CODRedemptionHunter (open-source code tracker, 6.0)";
        req.timeoutSeconds = 15;
        HttpResponse r = http_.send(req);
        if (r.status == 0) { last = "network error: " + r.error; wait(1 + 2 * attempt); continue; }
        if (r.status == 200 || r.status == 204) return {true, "sent"};
        if (r.status == 429) {
            double w = 2;
            try { w = Json::parse(r.body).get("retry_after").toDouble(2); }
            catch (...) { w = Json(r.header("retry-after")).toDouble(2); }
            if (w > 30) return {false, "Discord rate limit — retrying on the next scan (wait " + std::to_string(static_cast<int>(w)) + "s)"};
            wait(w + 0.25);
            last = "rate limited";
            continue;
        }
        if (r.status == 404) return {false, "Discord says this webhook doesn't exist (deleted?). Create a new one and paste it in Settings."};
        if (r.status == 401 || r.status == 403) return {false, "Discord rejected the webhook token — copy the full webhook URL again."};
        if (r.status == 400) return {false, "Discord rejected the message: " + truncateUtf8(r.body, 200)};
        last = "Discord answered HTTP " + std::to_string(r.status);
        wait(1 + 2 * attempt);
    }
    return {false, last};
}

std::pair<std::vector<long long>, std::string> DiscordNotifier::send(const std::string& url, const std::string& content,
                                                                     const std::vector<CodeRow>& rows, const std::string& kind) {
    std::vector<long long> delivered;
    std::string msg = "nothing to send";
    for (size_t i = 0; i < rows.size(); i += MAX_EMBEDS) {
        Json embeds = Json::array();
        std::vector<long long> ids;
        for (size_t k = i; k < std::min(rows.size(), i + MAX_EMBEDS); ++k) {
            embeds.push(codeEmbed(cfg_, store_.allSnippets(rows[k].id), rows[k], kind));
            ids.push_back(rows[k].id);
        }
        auto [ok, m] = post(url, message(i == 0 ? content : "", embeds));
        msg = m;
        if (!ok) break;
        delivered.insert(delivered.end(), ids.begin(), ids.end());
        wait(0.6);
    }
    return {delivered, msg};
}

void DiscordNotifier::log(bool ok, const std::string& text) {
    store_.logScan("Discord", ok ? "OK" : "ERROR", text);
    if (!ok) systemAlert("DISCORD_FAILED", text);
}

bool DiscordNotifier::wanted(const CodeRow& r) const {
    Json o = opts();
    if (r.region == "Garena" && !jbool(o, "include_garena", true)) return false;
    if (jstr(o, "new_codes", "all") == "likely") return r.checkVerdict == "LIKELY WORKS";
    return isLiveVerdict(r.checkVerdict);
}

std::vector<CodeRow> DiscordNotifier::rows() {
    std::vector<CodeRow> out;
    for (auto& r : store_.listCodes(20000))
        if (r.status != "REJECTED" && r.status != "IGNORED" && !r.checkVerdict.empty()) out.push_back(r);
    return out;
}

std::pair<int, std::string> DiscordNotifier::postGameList(const std::string& game, const std::vector<CodeRow>& all,
                                                          const std::string& url, bool includeDead, bool connect) {
    const std::string label = gameById(game).label();
    std::vector<CodeRow> live, dead;
    for (auto& r : all) if (wanted(r)) live.push_back(r);
    std::stable_sort(live.begin(), live.end(), [](auto& a, auto& b) {
        return std::make_pair(a.checkVerdict != "LIKELY WORKS", -a.checkScore) < std::make_pair(b.checkVerdict != "LIKELY WORKS", -b.checkScore);
    });
    size_t max = static_cast<size_t>(jinteger(opts(), "list_max", 25));
    if (live.size() > max) live.resize(max);
    if (includeDead)
        for (auto& r : all) if (isDeadVerdict(r.checkVerdict) && dead.size() < 60) dead.push_back(r);
    if (live.empty() && dead.empty()) {
        if (!connect) return {0, ""};
        auto [ok, msg] = post(url, message("📋 **" + label + ": connected.** No codes look usable right now — "
                                           "you'll get a message here as soon as a new one appears.", Json::array()));
        log(ok, ok ? label + ": connected (no usable codes to list)" : msg);
        return {0, ok ? "" : msg};
    }
    std::vector<long long> ids;
    if (!live.empty()) {
        auto [got, msg] = send(url, "📋 **Current " + label + " codes** — " + std::to_string(live.size()) +
                                        " that look like they work, best first:", live, "list");
        ids = got;
        if (ids.size() < live.size()) {
            log(false, label + ": list post failed: " + msg);
            return {static_cast<int>(ids.size()), msg};
        }
        store_.setDiscordState(ids, "live");
    }
    if (!dead.empty()) {
        std::vector<std::string> lines;
        for (auto& r : dead) lines.push_back("`" + r.code + "` — " + statusText(r));
        Json e = Json::object();
        e["title"] = label + ": expired / used up (" + std::to_string(dead.size()) + ")";
        e["color"] = RED;
        e["description"] = truncateUtf8(join(lines, "\n"), 4000);
        Json embeds = Json::array();
        embeds.push(e);
        auto [ok, msg] = post(url, message(live.empty() ? "📋 **" + label + "**" : "", embeds));
        if (!ok) return {static_cast<int>(ids.size()), msg};
    }
    log(true, label + ": posted the current list (" + std::to_string(ids.size()) + " code(s))");
    return {static_cast<int>(ids.size()), ""};
}

SyncResult DiscordNotifier::sync() {
    SyncResult result;
    Json o = opts();
    if (!jbool(o, "enabled", true)) return result;
    std::set<std::string> muted;
    for (const auto& [g, on] : o.get("games").items()) if (on.isBool() && !on.toBool()) muted.insert(g);
    std::map<std::string, std::vector<CodeRow>> byGame;
    for (auto& r : rows()) byGame[r.game.empty() ? "codm" : r.game].push_back(r);
    auto gameRows = [&](const std::string& g) {
        std::vector<CodeRow> out;
        for (auto& r : rows()) if ((r.game.empty() ? "codm" : r.game) == g) out.push_back(r);
        return out;
    };
    for (auto& [game, initial] : byGame) {
        if (muted.count(game)) continue;
        std::vector<CodeRow> list = initial;
        std::string url = route(game, "codes");
        if (url.empty()) continue;
        const std::string label = gameById(game).label();
        if (!validWebhook(url)) {
            log(false, label + ": webhook isn't a Discord webhook URL");
            result.error = "invalid webhook URL";
            continue;
        }
        std::string key = sha256Hex(url).substr(0, 16);
        std::string stateKey = "discord_connected:" + game;
        bool legacy = game == "codm" && store_.getState("discord_connected_to") == key;
        if (store_.getState(stateKey) != key && !legacy) {
            if (jbool(o, "post_list_on_connect", true)) {
                auto [n, err] = postGameList(game, list, url, false, true);
                if (!err.empty()) { result.error = err; continue; }
                result.listed += n;
            }
            std::vector<long long> unknown;
            for (auto& r : gameRows(game)) if (r.discordState.empty()) unknown.push_back(r.id);
            store_.setDiscordState(unknown, "known");
            store_.setState(stateKey, key);
            list = gameRows(game);
        } else if (legacy) {
            store_.setState(stateKey, key);
        }
        if (jbool(o, "post_new", true)) {
            std::vector<CodeRow> fresh;
            std::vector<long long> quiet;
            for (auto& r : list) {
                if (!r.discordState.empty()) continue;
                if (r.fresh && wanted(r)) fresh.push_back(r);
                else if (!r.fresh || isDeadVerdict(r.checkVerdict)) quiet.push_back(r.id);
            }
            store_.setDiscordState(quiet, "known");
            if (!fresh.empty()) {
                std::stable_sort(fresh.begin(), fresh.end(), [](auto& a, auto& b) { return a.checkScore > b.checkScore; });
                auto [ids, msg] = send(url, "🆕 **" + std::to_string(fresh.size()) + " new " + label + " code" +
                                                (fresh.size() > 1 ? "s" : "") + "**", fresh, "new");
                store_.setDiscordState(ids, "live");
                result.posted += static_cast<int>(ids.size());
                bool all = ids.size() == fresh.size();
                log(all, label + ": posted " + std::to_string(ids.size()) + "/" + std::to_string(fresh.size()) + " new code(s)" +
                             (all ? "" : " — " + msg + "; the rest retry next scan"));
                if (!all) result.error = msg;
            }
        }
        if (jbool(o, "post_updates", true)) {
            std::vector<CodeRow> died;
            for (auto& r : list) if (r.discordState == "live" && isDeadVerdict(r.checkVerdict)) died.push_back(r);
            if (!died.empty()) {
                auto [ids, msg] = send(route(game, "updates"), "⚠ **" + label + " update** — these no longer look usable:", died, "update");
                store_.setDiscordState(ids, "dead");
                result.updates += static_cast<int>(ids.size());
                bool all = ids.size() == died.size();
                log(all, label + ": posted " + std::to_string(ids.size()) + " expired/used-up update(s)" + (all ? "" : " — " + msg));
                if (!all) result.error = msg;
            }
        }
    }
    return result;
}

std::pair<int, std::string> DiscordNotifier::postList(bool includeDead, const std::string& game) {
    std::map<std::string, std::vector<CodeRow>> byGame;
    for (auto& r : rows()) byGame[r.game].push_back(r);
    std::vector<std::string> games;
    if (!game.empty()) games.push_back(game);
    else for (auto& [g, _] : byGame) games.push_back(g);
    int total = 0;
    std::string error;
    bool anyUrl = false;
    for (auto& g : games) {
        std::string url = route(g, "codes");
        if (url.empty()) continue;
        anyUrl = true;
        auto [n, err] = postGameList(g, byGame[g], url, includeDead, !game.empty());
        total += n;
        if (error.empty()) error = err;
    }
    if (!anyUrl) return {0, "No Discord webhook set."};
    return {total, error};
}

std::pair<bool, std::string> DiscordNotifier::test(const std::string& urlIn) {
    std::string url = trim(urlIn.empty() ? route("codm", "codes") : urlIn);
    if (url.empty()) return {false, "No Discord webhook set — paste it in Settings → Discord."};
    if (!validWebhook(url))
        return {false, "That isn't a Discord webhook URL. In Discord: Server Settings → Integrations → Webhooks → New Webhook → Copy Webhook URL."};
    CodeRow sample;
    sample.code = "BTVJZBZ74W"; sample.region = "Global"; sample.checkVerdict = "LIKELY WORKS"; sample.checkScore = 75;
    sample.sourceCount = 2; sample.firstSeen = utcNowIso(); sample.status = "READY"; sample.game = "codm";
    Json embed = codeEmbed(cfg_, {"[Example] BTVJZBZ74W - Bathysphere Killer Whale"}, sample, "new");
    embed["title"] = "✅ Test — this is how codes will look";
    Json payload = Json::object();
    payload["username"] = jstr(opts(), "username", "COD Redemption Hunter");
    payload["content"] = "Webhook connected.";
    Json embeds = Json::array();
    embeds.push(embed);
    payload["embeds"] = embeds;
    Json allowed = Json::object();
    allowed["parse"] = Json::array();
    payload["allowed_mentions"] = allowed;
    auto [ok, msg] = post(url, payload);
    return {ok, ok ? "Test message sent — check your Discord channel." : msg};
}

void DiscordNotifier::systemAlert(const std::string& event, const std::string& details) {
    static const std::map<std::string, std::string> events = {
        {"SERVICE_STARTED", "🟢 Background scanner started"}, {"SERVICE_STOPPED", "⚪ Background scanner stopped"},
        {"SOURCE_BLOCKED", "🟠 A site asked the scanner to back off"}, {"CIRCUIT_OPEN", "🔴 Redemption paused (circuit breaker)"},
        {"EMERGENCY_STOP", "🛑 Emergency stop pressed"}, {"REDEMPTION_RESULT", "🎟 Redemption result"},
        {"DISCORD_FAILED", "⚠ Discord delivery problem"}};
    std::string url = webhookFor(cfg_, "", "system");
    auto ev = events.find(event);
    if (ev == events.end() || !validWebhook(url)) return;
    if (event == "SOURCE_BLOCKED" || event == "DISCORD_FAILED") {
        auto last = parseIso(store_.getState("system_alert:" + event));
        if (last && nowSeconds() - *last < 30 * 60) return;
        store_.setState("system_alert:" + event, utcNowIso());
    }
    Json e = Json::object();
    e["title"] = ev->second;
    e["description"] = details.empty() ? "\xE2\x80\x94" : truncateUtf8(details, 1500);
    e["color"] = (event == "CIRCUIT_OPEN" || event == "EMERGENCY_STOP" || event == "DISCORD_FAILED") ? RED : GREY;
    e["timestamp"] = utcNowIso();
    Json p = Json::object();
    p["username"] = "COD Redemption Hunter · system";
    Json allowed = Json::object();
    allowed["parse"] = Json::array();
    p["allowed_mentions"] = allowed;
    Json embeds = Json::array();
    embeds.push(e);
    p["embeds"] = embeds;
    post(url, p, 2);  // the caller is already on a worker thread
}

}  // namespace hunter
