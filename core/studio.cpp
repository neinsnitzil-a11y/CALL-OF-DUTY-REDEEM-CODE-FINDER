#include "studio.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#include "content.hpp"
#include "games.hpp"
#include "util.hpp"

namespace hunter {

namespace {
constexpr int kBuildColor = 0xF5A623, kResearchColor = 0x33CCFF, kSettingsColor = 0x8B5CF6;

Json fieldOf(const std::string& name, const std::string& value, bool inl) {
    Json f = Json::object();
    f["name"] = name;
    f["value"] = truncateUtf8(value, 1024);
    f["inline"] = inl;
    return f;
}
std::string listOf(const Json& arr) {
    std::vector<std::string> v;
    for (auto& x : arr.elements()) v.push_back(x.toString());
    return join(v, ", ");
}
Json footer(const std::string& text) {
    Json f = Json::object();
    f["text"] = truncateUtf8(text, 2048);
    return f;
}
std::string fileName(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}
}  // namespace

Json buildEmbed(const Json& b, const std::string& label, bool researched) {
    std::vector<std::string> att;
    std::string title, desc, url;
    if (researched) {
        for (auto& a : b.get("attachments").elements()) {
            long long v = a.size() > 2 ? a.elements()[2].toInt() : 1;
            att.push_back("**" + a.elements()[0].toString() + ":** " + a.elements()[1].toString() +
                          (v > 1 ? "  ·  " + std::to_string(v) + " sites" : ""));
        }
        title = "🔬 " + label + ": " + jstr(b, "weapon") + (jbool(b, "verified") ? "  ✔ verified" : "");
        std::vector<std::string> doms;
        for (auto& d : b.get("domains").elements()) if (doms.size() < 5) doms.push_back(d.toString());
        desc = "Seen on " + std::to_string(jinteger(b, "sites")) + " site(s): " + join(doms, ", ");
        if (b.get("sources").size()) url = jstr(b.get("sources").elements()[0], "url");
    } else {
        for (auto& a : b.get("attachments").elements())
            att.push_back("**" + a.elements()[0].toString() + ":** " + a.elements()[1].toString());
        title = "🔫 " + label + ": " + jstr(b, "weapon") + (jstr(b, "tier").empty() ? "" : " (" + jstr(b, "tier") + " tier)");
        std::vector<std::string> parts;
        for (const char* k : {"class", "role"}) if (!jstr(b, k).empty()) parts.push_back(jstr(b, k));
        desc = join(parts, " · ");
        url = jstr(b, "source_url");
    }
    Json fields = Json::array();
    if (!att.empty()) fields.push(fieldOf("Attachments", join(att, "\n"), false));
    if (b.get("perks").size()) fields.push(fieldOf("Perks", listOf(b.get("perks")), true));
    if (b.get("equipment").size()) fields.push(fieldOf("Equipment", listOf(b.get("equipment")), true));
    if (!jstr(b, "secondary").empty()) fields.push(fieldOf("Secondary", jstr(b, "secondary"), true));
    if (att.empty()) fields.push(fieldOf("Attachments", "See the source for the full build.", false));
    std::string src = jstr(b, "source");
    if (src.empty() && researched) src = std::to_string(b.get("sources").size()) + " source page(s)";
    Json e = Json::object();
    e["title"] = truncateUtf8(title, 256);
    e["description"] = truncateUtf8(desc, 4000);
    e["color"] = researched ? kResearchColor : kBuildColor;
    e["fields"] = fields;
    e["footer"] = footer("COD Redemption Hunter · " + src);
    e["timestamp"] = utcNowIso();
    if (startsWith(url, "https://")) e["url"] = url;
    return e;
}

Json settingsEmbed(const Json& section, const std::string& label, const std::string& title) {
    std::vector<std::string> lines;
    for (auto& item : section.get("items").elements())
        if (item.size() >= 2) lines.push_back("**" + item.elements()[0].toString() + ":** " + item.elements()[1].toString());
    Json e = Json::object();
    e["title"] = truncateUtf8("⚙ " + label + " — " + jstr(section, "name"), 256);
    e["description"] = truncateUtf8(join(lines, "\n"), 4000);
    e["color"] = kSettingsColor;
    e["footer"] = footer("COD Redemption Hunter · " + title);
    e["timestamp"] = utcNowIso();
    return e;
}

Json shotEmbed(const Json& m, const std::string& label, const std::string& uploadName) {
    bool own = jstr(m, "added_by") != "research";
    Json e = Json::object();
    e["title"] = truncateUtf8("🖼 " + label + ": " + (jstr(m, "weapon").empty() ? "build screenshot" : jstr(m, "weapon")), 256);
    e["color"] = kBuildColor;
    e["description"] = own ? "Shared by the community" + (jstr(m, "note").empty() ? "" : " — " + jstr(m, "note"))
                           : "Screenshot from **" + jstr(m, "site", "source") + "** — full guide: " + jstr(m, "source_page");
    e["footer"] = footer("COD Redemption Hunter" + (own ? "" : " · credit: " + jstr(m, "site")));
    e["timestamp"] = utcNowIso();
    Json img = Json::object();
    if (!uploadName.empty()) img["url"] = "attachment://" + uploadName;
    else if (startsWith(jstr(m, "image_url"), "https://")) img["url"] = jstr(m, "image_url");
    if (img.size()) e["image"] = img;
    if (!own && startsWith(jstr(m, "source_page"), "https://")) e["url"] = jstr(m, "source_page");
    return e;
}

std::vector<StudioMessage> studioMessages(const Config& cfg, Store& store, const std::string& kind, const std::string& gameIdIn,
                                          const Json& items, const std::string& content, bool mention,
                                          const std::string& settingsTitle) {
    std::string gameId = gameIdIn.empty() ? "warzone" : gameIdIn;
    std::string label = gameById(gameId).label();
    std::vector<Json> embeds;
    std::vector<std::tuple<std::string, std::string, std::string>> uploads;  // matched to embeds below
    std::vector<std::string> uploadFor;                                      // per embed: file name or ""
    for (const auto& it : items.elements()) {
        std::string up;
        if (kind == "codes" || kind == "queue") {
            auto row = store.getCode(it.toInt());
            if (!row) continue;
            embeds.push_back(codeEmbed(cfg, store.allSnippets(row->id), *row, kind == "queue" ? "new" : "list"));
        } else if (kind == "builds" || kind == "research") {
            embeds.push_back(buildEmbed(it, label, kind == "research"));
        } else if (kind == "settings") {
            embeds.push_back(settingsEmbed(it, label, settingsTitle));
        } else if (kind == "shots") {
            bool own = jstr(it, "added_by") != "research";
            if (own || jstr(it, "image_url").empty()) {
                up = fileName(jstr(it, "file"));
                std::string bytes = readFile(jstr(it, "file"));
                if (bytes.empty()) up.clear();
                else uploads.emplace_back(up, bytes, jstr(it, "mime", "image/png"));
            }
            embeds.push_back(shotEmbed(it, gameById(jstr(it, "game", gameId)).label(), up));
        }
        uploadFor.push_back(up);
    }
    Json o = cfg.snapshot().get("discord");
    std::string username = jstr(o, "username", "COD Redemption Hunter");
    std::string mentionText = trim(jstr(o, "mention"));
    std::vector<StudioMessage> out;
    size_t n = std::max<size_t>(1, embeds.size());
    for (size_t i = 0; i < n; i += 10) {
        StudioMessage m;
        Json p = Json::object();
        p["username"] = truncateUtf8(username, 80);
        std::string text = i == 0 ? content : "";
        Json allowed = Json::object();
        allowed["parse"] = Json::array();
        if (mention && i == 0 && !mentionText.empty()) {
            if (mentionText == "@everyone" || mentionText == "@here") {
                allowed["parse"].push("everyone");
                text = mentionText + " " + text;
            } else if (std::all_of(mentionText.begin(), mentionText.end(), ::isdigit)) {
                Json roles = Json::array();
                roles.push(mentionText);
                allowed["roles"] = roles;
                text = "<@&" + mentionText + "> " + text;
            }
        }
        p["content"] = truncateUtf8(trim(text), 2000);
        Json batch = Json::array();
        for (size_t k = i; k < std::min(embeds.size(), i + 10); ++k) {
            batch.push(embeds[k]);
            if (!uploadFor[k].empty())
                for (auto& u : uploads)
                    if (std::get<0>(u) == uploadFor[k] &&
                        std::none_of(m.files.begin(), m.files.end(), [&](auto& f) { return std::get<0>(f) == uploadFor[k]; }))
                        m.files.push_back(u);
        }
        p["embeds"] = batch;
        p["allowed_mentions"] = allowed;
        m.payload = p;
        if (!jstr(p, "content").empty() || batch.size()) out.push_back(m);
    }
    return out;
}

std::string studioPreview(const std::vector<StudioMessage>& msgs) {
    if (msgs.empty()) return "Nothing selected.";
    std::vector<std::string> out;
    for (size_t i = 0; i < msgs.size(); ++i) {
        const Json& p = msgs[i].payload;
        out.push_back("── message " + std::to_string(i + 1) + " of " + std::to_string(msgs.size()) + " · posted as “" +
                      jstr(p, "username") + "” ──");
        if (!jstr(p, "content").empty()) out.push_back(jstr(p, "content"));
        for (auto& e : p.get("embeds").elements()) {
            out.push_back("\n▌ " + jstr(e, "title") + (jstr(e, "url").empty() ? "" : "\n▌ (" + jstr(e, "url") + ")"));
            std::string img = jstr(e.get("image"), "url");
            if (!img.empty())
                out.push_back("▌ [image: " + (startsWith(img, "attachment://") ? "uploaded file " + img.substr(13) : img) + "]");
            for (auto& l : splitLines(replaceAll(jstr(e, "description"), "**", ""))) if (!l.empty()) out.push_back("▌ " + l);
            for (auto& f : e.get("fields").elements()) {
                out.push_back("▌ " + jstr(f, "name") + ":");
                for (auto& l : splitLines(replaceAll(jstr(f, "value"), "**", ""))) out.push_back("▌   " + l);
            }
        }
        if (!msgs[i].files.empty()) out.push_back("(uploads " + std::to_string(msgs[i].files.size()) + " image file(s))");
        out.push_back("");
    }
    return join(out, "\n");
}

std::string studioDestination(const Config& cfg, const std::string& dest, const std::string& gameId, const std::string& custom) {
    if (dest == "custom") return trim(custom);
    if (dest == "shared") return cfg.env("DISCORD_WEBHOOK_URL");
    return webhookFor(cfg, gameId.empty() ? "other" : gameId, dest);
}

std::pair<std::string, std::string> multipartBody(const Json& payload,
                                                  const std::vector<std::tuple<std::string, std::string, std::string>>& files) {
    std::string boundary = "----CODHunter" + sha256Hex(payload.dump() + std::to_string(nowSeconds())).substr(0, 24);
    std::string body;
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"payload_json\"\r\nContent-Type: application/json\r\n\r\n";
    body += payload.dump() + "\r\n";
    for (size_t i = 0; i < files.size(); ++i) {
        auto& [name, bytes, mime] = files[i];
        body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"files[" + std::to_string(i) + "]\"; filename=\"" +
                replaceAll(name, "\"", "") + "\"\r\nContent-Type: " + mime + "\r\n\r\n";
        body += bytes + "\r\n";
    }
    body += "--" + boundary + "--\r\n";
    return {body, "multipart/form-data; boundary=" + boundary};
}

std::pair<bool, std::string> studioSend(Config& cfg, Store& store, HttpClient& http, const std::string& url,
                                        const std::vector<StudioMessage>& msgs, const std::string& kind,
                                        const std::vector<long long>& codeIds) {
    if (!validWebhook(url)) return {false, "That isn't a Discord webhook URL (Server Settings → Integrations → Webhooks)."};
    DiscordNotifier notifier(cfg, store, http);
    for (size_t i = 0; i < msgs.size(); ++i) {
        std::pair<bool, std::string> res;
        if (msgs[i].files.empty()) {
            res = notifier.post(url, msgs[i].payload);
        } else {
            res = {false, "unknown error"};
            for (int attempt = 0; attempt < 3; ++attempt) {
                auto [body, ctype] = multipartBody(msgs[i].payload, msgs[i].files);
                HttpRequest req;
                req.method = "POST";
                req.url = url + (url.find('?') == std::string::npos ? "?wait=true" : "&wait=true");
                req.body = body;
                req.contentType = ctype;
                req.timeoutSeconds = 60;
                HttpResponse r = http.send(req);
                if (r.status == 200 || r.status == 204) { res = {true, "sent"}; break; }
                if (r.status == 429) {
                    double w = 2;
                    try { w = std::min(30.0, Json::parse(r.body).get("retry_after").toDouble(2)); } catch (...) {}
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>((w + 0.25) * 1000)));
                    res = {false, "rate limited"};
                    continue;
                }
                if (r.status == 413) { res = {false, "Discord says the image is too big for this server (limit is usually 10 MB)."}; break; }
                res = {false, r.status ? "Discord answered HTTP " + std::to_string(r.status) + ": " + truncateUtf8(r.body, 160)
                                       : "network error: " + r.error};
                if (r.status) break;
            }
        }
        if (!res.first) {
            store.logScan("Discord", "ERROR", "studio: " + res.second);
            return {false, "Sent " + std::to_string(i) + " of " + std::to_string(msgs.size()) + " message(s): " + res.second};
        }
        if (i + 1 < msgs.size()) std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }
    if (!codeIds.empty()) store.setDiscordState(codeIds, kind == "queue" ? "live" : "known");
    store.logScan("Discord", "OK", "studio: sent " + std::to_string(msgs.size()) + " message(s) (" + kind + ")");
    return {true, "Sent " + std::to_string(msgs.size()) + " message(s) to Discord."};
}

}  // namespace hunter
