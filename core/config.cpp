#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <regex>
#include <set>
#include <fstream>
#include <sstream>

#include "util.hpp"

namespace hunter {

const std::vector<std::pair<std::string, std::string>> kEnvKeys = {
    {"CODM_UID", "Your CODM Player UID (digits only). NOT a password."},
    {"ACCOUNT_REGION", "Global, Garena or Both"},
    {"REDEMPTION_URL_GLOBAL", "Official Global redemption page (verify it yourself)"},
    {"REDEMPTION_URL_GARENA", "Official Garena redemption page (verify it yourself)"},
    {"BROWSER_CHANNEL", "chrome, msedge or chromium"},
    {"DISCORD_WEBHOOK_URL", "Discord webhook for new-code posts (keep private)"},
    {"GOOGLE_API_KEY", "Optional Google Programmable Search key"},
    {"GOOGLE_CSE_ID", "Optional Google Programmable Search engine ID"},
    {"AI_PROVIDER", "AI helper provider"},
    {"AI_BASE_URL", "AI helper API base URL"},
    {"AI_API_KEY", "AI helper API key (keep private)"},
    {"AI_MODEL", "AI helper model"},
    {"NTFY_TOPIC", "ntfy.sh topic for phone push (keep private)"},
    {"GITHUB_TOKEN", "GitHub token for community builds (keep private)"},
    {"APP_LANGUAGE", "Language code, e.g. en, es, pt"},
};

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    if (startsWith(s, "\xEF\xBB\xBF")) s = s.substr(3);  // BOM from Notepad
    return s;
}

void writeFileAtomic(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path());
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("can't write " + tmp.string());
        out << data;
    }
    fs::rename(tmp, p);
}

fs::path Paths::defaultDataDir() {
    if (const char* o = std::getenv("HUNTER_DATA_DIR"); o && *o) return fs::path(o);
#ifdef _WIN32
    if (const char* la = std::getenv("LOCALAPPDATA"); la && *la) return fs::path(la) / "COD Redemption Hunter";
#endif
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::path(x) / "cod-redemption-hunter";
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    return fs::path(home ? home : ".") / ".local" / "share" / "cod-redemption-hunter";
}

std::map<std::string, std::string> Config::parseEnvFile(const fs::path& p) {
    std::map<std::string, std::string> v;
    for (auto line : splitLines(readFile(p))) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string val = trim(line.substr(eq + 1));
        if (val.size() >= 2 && (val.front() == '"' || val.front() == '\'') && val.back() == val.front())
            val = val.substr(1, val.size() - 2);
        v[trim(line.substr(0, eq))] = val;
    }
    return v;
}

Config::Config(Paths paths) : paths_(std::move(paths)) {
    fs::create_directories(paths_.dataDir);
    fs::create_directories(paths_.logs());
    fs::create_directories(paths_.database().parent_path());
    if (!fs::exists(paths_.config()) && fs::exists(paths_.appDir / "config.json"))
        fs::copy_file(paths_.appDir / "config.json", paths_.config());
    if (!fs::exists(paths_.env()) && fs::exists(paths_.appDir / ".env.example"))
        fs::copy_file(paths_.appDir / ".env.example", paths_.env());
    reload();
    mergeShippedDefaults();
}

void Config::reload() {
    Json cfg = Json::object();
    std::string text = readFile(paths_.config());
    if (!text.empty()) cfg = Json::parse(text);
    auto env = parseEnvFile(paths_.env());
    std::lock_guard lock(mu_);
    cfg_ = std::move(cfg);
    env_ = std::move(env);
}

namespace {
bool mergeDefaults(Json& user, const Json& shipped) {
    bool changed = false;
    for (const auto& [k, v] : shipped.items()) {
        if (!user.contains(k)) { user[k] = v; changed = true; }
        else if (v.isObject() && user.get(k).isObject()) changed |= mergeDefaults(user[k], v);
    }
    return changed;
}
}  // namespace

void Config::mergeShippedDefaults() {
    fs::path shippedPath = paths_.appDir / "config.json";
    std::error_code ec;
    if (!fs::exists(shippedPath) || fs::equivalent(paths_.dataDir, paths_.appDir, ec)) return;
    Json shipped;
    try { shipped = Json::parse(readFile(shippedPath)); } catch (...) { return; }
    Json user = snapshot();
    bool changed = mergeDefaults(user, shipped);
    std::set<std::string> have;
    for (const auto& s : user.get("sources").elements()) have.insert(jstr(s, "url", jstr(s, "name")));
    for (const auto& s : shipped.get("sources").elements())
        if (!have.count(jstr(s, "url", jstr(s, "name")))) { user["sources"].push(s); changed = true; }
    if (jstr(user, "version") != jstr(shipped, "version")) { user["version"] = shipped.get("version"); changed = true; }
    if (changed) save(user);
}

Json Config::snapshot() const {
    std::lock_guard lock(mu_);
    return cfg_;
}

void Config::save(const Json& cfg) {
    std::lock_guard lock(mu_);
    writeFileAtomic(paths_.config(), cfg.dump(2) + "\n");
    cfg_ = cfg;
}

std::string Config::env(const std::string& key, const std::string& fallback) const {
    if (const char* o = std::getenv(key.c_str()); o && *o) return trim(o);  // process env wins, like os.getenv
    std::lock_guard lock(mu_);
    auto it = env_.find(key);
    return it != env_.end() ? trim(it->second) : fallback;
}

std::map<std::string, std::string> Config::envValues() const {
    std::lock_guard lock(mu_);
    return env_;
}

void Config::writeEnv(const std::map<std::string, std::string>& updates) {
    auto clean = [](std::string v) { return trim(replaceAll(replaceAll(v, "\n", ""), "\r", "")); };
    std::lock_guard lock(mu_);
    std::map<std::string, std::string> remaining = updates;
    std::vector<std::string> out;
    static const std::regex key = re(R"(^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=)");
    for (const auto& line : splitLines(readFile(paths_.env()))) {
        std::smatch m;
        if (std::regex_search(line, m, key) && remaining.count(m[1].str())) {
            out.push_back(m[1].str() + "=" + clean(remaining[m[1].str()]));
            remaining.erase(m[1].str());
        } else {
            out.push_back(line);
        }
    }
    while (!out.empty() && out.back().empty()) out.pop_back();
    for (const auto& [k, v] : remaining) {
        for (const auto& [ek, comment] : kEnvKeys)
            if (ek == k) out.push_back("# " + comment);
        out.push_back(k + "=" + clean(v));
    }
    writeFileAtomic(paths_.env(), join(out, "\n") + "\n");
    for (const auto& [k, v] : updates) env_[k] = clean(v);
}

std::string Config::redemptionUrl(const std::string& region) const {
    std::string r = lower(region);
    if (r == "garena") return env("REDEMPTION_URL_GARENA");
    if (r == "global") return env("REDEMPTION_URL_GLOBAL");
    std::string g = env("REDEMPTION_URL_GLOBAL");
    return g.empty() ? env("REDEMPTION_URL_GARENA") : g;
}

std::vector<std::string> Config::accountRegions() const {
    std::string r = lower(env("ACCOUNT_REGION", "Both"));
    if (r == "global") return {"Global"};
    if (r == "garena") return {"Garena"};
    return {"Global", "Garena"};
}

int Config::pollIntervalSeconds() const {
    Json c = snapshot();
    const Json& d = c.get("discovery");
    return static_cast<int>(std::max(jinteger(d, "min_poll_interval_seconds", 120), jinteger(d, "poll_interval_seconds", 300)));
}

Json Config::runtime(const std::string& key) const {
    static const Json defaults = Json::parse(R"({"monitor_mode":"dashboard","start_monitoring_on_launch":true,
        "keep_awake":true,"windows_notifications":true,"auto_open_ready_codes":false,"theme":"dark",
        "minimize_to_tray":true})");
    Json c = snapshot();
    const Json& v = c.get("runtime").get(key);
    return v.isNull() ? defaults.get(key) : v;
}

std::string Config::version() const { return jstr(snapshot(), "version", "0.0.0"); }

}  // namespace hunter
