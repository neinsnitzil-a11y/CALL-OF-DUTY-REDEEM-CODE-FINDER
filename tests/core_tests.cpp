// Offline tests for the C++ core. Parity tests compare against golden.json, which was produced by
// the Python version on the same pages and texts, so both versions read pages identically.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../core/checker.hpp"
#include "../core/config.hpp"
#include "../core/discord.hpp"
#include "../core/discovery.hpp"
#include "../core/engine.hpp"
#include "../core/extraction.hpp"
#include "../core/games.hpp"
#include "../core/html.hpp"
#include "../core/json.hpp"
#include "../core/protection.hpp"
#include "../core/util.hpp"
#include "../core/validation.hpp"
#include "../core/ai.hpp"
#include "../core/content.hpp"
#include "../core/helper.hpp"
#include "../core/research.hpp"
#include "../core/studio.hpp"
#include "../core/community.hpp"
#include "../core/i18n.hpp"
#include "../core/patches.hpp"
#include "../core/stats.hpp"

using namespace hunter;
namespace fs = std::filesystem;

static int g_failed = 0, g_checks = 0;
static std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> r;
    return r;
}
struct Reg {
    Reg(const char* n, std::function<void()> f) { registry().emplace_back(n, std::move(f)); }
};
#define TEST(name) static void name(); static Reg reg_##name(#name, name); static void name()
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failed; std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } } while (0)
#define CHECK_EQ(a, b) do { ++g_checks; auto _a = (a); auto _b = (b); if (!(_a == _b)) { ++g_failed; \
    std::cerr << "  FAIL " << __LINE__ << ": " #a " == " #b "\n    got: " << _a << "\n    want: " << _b << "\n"; } } while (0)

#ifdef HUNTER_SOURCE_DIR
static fs::path kRoot = HUNTER_SOURCE_DIR;
#else
static fs::path kRoot = fs::path(__FILE__).parent_path().parent_path();
#endif

// ── fakes ───────────────────────────────────────────────────────
struct FakeHttp : HttpClient {
    std::map<std::string, HttpResponse> pages;     // url → response
    std::vector<HttpRequest> sent;
    std::function<HttpResponse(const HttpRequest&)> handler;
    HttpResponse send(const HttpRequest& req) override {
        sent.push_back(req);
        if (handler) return handler(req);
        auto it = pages.find(req.url);
        if (it != pages.end()) { HttpResponse r = it->second; if (r.finalUrl.empty()) r.finalUrl = req.url; return r; }
        HttpResponse r;
        r.status = 404;
        return r;
    }
    void page(const std::string& url, const std::string& body, int status = 200, std::map<std::string, std::string> h = {}) {
        HttpResponse r;
        r.status = status;
        r.body = body;
        r.headers = std::move(h);
        pages[url] = r;
    }
};

struct TempEnv {
    fs::path dir;
    TempEnv() {
        dir = fs::temp_directory_path() / ("hunter_test_" + std::to_string(nowSeconds()) + "_" + std::to_string(rand()));
        fs::create_directories(dir / "app");
        fs::copy_file(kRoot / "data" / "config.json", dir / "app" / "config.json");
        std::ofstream(dir / "app" / ".env.example") << "# example\nCODM_UID=\nACCOUNT_REGION=Both\n";
    }
    ~TempEnv() { std::error_code ec; fs::remove_all(dir, ec); }
    Paths paths() const { return Paths{dir / "app", dir / "data"}; }
};

// Minimal config with chosen sources (keeps tests independent of the shipped source list).
static void useSources(Config& cfg, const std::string& sourcesJson) {
    Json c = cfg.snapshot();
    c["sources"] = Json::parse(sourcesJson);
    c["discovery"]["delay_between_sources_seconds"] = 0;
    c["discord"]["enabled"] = true;
    cfg.save(c);
}

static std::string candKey(const std::string& code, const std::string& region, const std::string& report,
                           const std::string& exp, const std::string& game, const std::string& platform, const std::string& kind) {
    return code + "|" + region + "|" + report + "|" + exp + "|" + game + "|" + platform + "|" + kind;
}
static std::vector<std::string> keysOf(const std::vector<Candidate>& cands) {
    std::vector<std::string> out;
    for (auto& c : cands) out.push_back(candKey(c.code, c.region, c.report, c.expiresAt.value_or(""), c.gameHint, c.platform, c.kind));
    return out;
}
static std::vector<std::string> keysOf(const Json& cands) {
    std::vector<std::string> out;
    for (auto& c : cands.elements())
        out.push_back(candKey(jstr(c, "code"), jstr(c, "region"), jstr(c, "report"), jstr(c, "expires_at"), jstr(c, "game_hint"),
                              jstr(c, "platform"), jstr(c, "kind")));
    return out;
}
static void compareLists(const std::string& label, const std::vector<std::string>& got, const std::vector<std::string>& want) {
    ++g_checks;
    if (got == want) return;
    ++g_failed;
    std::cerr << "  FAIL parity: " << label << " (" << got.size() << " vs " << want.size() << ")\n";
    std::set<std::string> g(got.begin(), got.end()), w(want.begin(), want.end());
    int shown = 0;
    for (auto& x : w) if (!g.count(x) && shown++ < 8) std::cerr << "    missing: " << x << "\n";
    for (auto& x : g) if (!w.count(x) && shown++ < 16) std::cerr << "    extra:   " << x << "\n";
    if (g == w) std::cerr << "    (same items, different order)\n";
}

static const Json& golden() {
    static Json g = Json::parse(readFile(kRoot / "tests" / "golden.json"));
    return g;
}

// ── basics ──────────────────────────────────────────────────────
TEST(json_roundtrip) {
    Json j = Json::parse(R"({"a":1,"b":[true,null,"xé\n"],"c":{"d":-2.5},"e":"🎯"})");
    CHECK_EQ(jinteger(j, "a"), 1LL);
    CHECK_EQ(j.get("b").elements()[2].toString(), std::string("x\xC3\xA9\n"));
    CHECK_EQ(j.get("c").get("d").toDouble(), -2.5);
    CHECK_EQ(jstr(j, "e"), std::string("\xF0\x9F\x8E\xAF"));
    CHECK(Json::parse(j.dump(2)) == j);
    CHECK(Json::parse(j.dump()) == j);
    bool threw = false;
    try { Json::parse("{bad"); } catch (...) { threw = true; }
    CHECK(threw);
}

TEST(sha256_and_utils) {
    CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(decodeEntities("A&amp;B &#39;x&#x2019; &nbsp;"), std::string("A&B 'x\xE2\x80\x99 \xC2\xA0"));
    CHECK_EQ(collapseSpaces("  a \t b\xC2\xA0 c  "), std::string("a b c"));
    CHECK_EQ(urlDomain("https://WWW.Example.com:443/a?b"), std::string("example.com"));
    CHECK_EQ(realResultUrl("https://www.google.com/url?rct=j&url=https%3A%2F%2Fsite.com%2Fp&ct=ga"), std::string("https://site.com/p"));
    auto t = parseIso("2026-11-23T10:00:00+00:00");
    CHECK(t.has_value());
    CHECK_EQ(utcIso(*t), std::string("2026-11-23T10:00:00+00:00"));
    CHECK_EQ(*parseIso("2026-11-23T12:00:00+02:00"), *t);
    CHECK_EQ(maskUid("1234567890123456789"), std::string("1234\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2" "6789"));
}

TEST(tokens_match_python_lookarounds) {
    auto t = findTokens("call-of-duty-ABCDEF12 x BTVJZBZ74W- Nomad, abcDEFGHIJ, CODM7N6T1AW/x, @QWERTY12");
    std::vector<std::string> codes;
    for (auto& m : t) codes.push_back(m.code);
    CHECK_EQ(join(codes, ","), std::string("BTVJZBZ74W"));
    auto d = findDashed("XBLX3-HN7X7-7NAH7 and x-SX4G-73D55-RNJ7 and AAAA-BBBBB-CCCC-DDDDDD");
    CHECK_EQ(d.size(), size_t(1));
    CHECK_EQ(d.front().code, std::string("XBLX3-HN7X7-7NAH7"));
}

// ── parity with the Python version ──────────────────────────────
TEST(parity_live_pages) {
    std::map<std::string, std::string> urls = {
        {"pocketgamer.html", "https://www.pocketgamer.com/call-of-duty-mobile/codes/"},
        {"pockettactics.html", "https://www.pockettactics.com/cod-mobile/redeem-codes"},
        {"sportsdunia.html", "https://www.sportsdunia.com/gaming/cod-mobile-redeem-codes"},
        {"allthingshow.html", "https://allthings.how/call-of-duty-mobile-codes/"}};
    for (auto& [file, url] : urls) {
        Document doc = htmlToDocument(readFile(kRoot / "tests" / "fixtures" / file), url);
        const Json& want = golden().get("pages").get(file);
        CHECK_EQ(doc.title, jstr(want, "title"));
        compareLists(file, keysOf(extractFromDocument(doc)), keysOf(want.get("cands")));
    }
}

TEST(parity_texts) {
    for (auto& t : golden().get("texts").elements())
        compareLists(jstr(t, "text").substr(0, 40), keysOf(extractCandidates(jstr(t, "text"))), keysOf(t.get("cands")));
}

TEST(parity_validation) {
    Json cfg = Json::parse(readFile(kRoot / "data" / "config.json"));
    for (auto& v : golden().get("validate").elements()) {
        Validation got = validateCode(jstr(v, "code"), cfg);
        std::vector<std::string> want;
        for (auto& r : v.get("reasons").elements()) want.push_back(r.toString());
        if (got.valid != jbool(v, "valid") || got.reasons != want) {
            ++g_failed;
            std::cerr << "  FAIL validate " << jstr(v, "code") << ": got " << got.valid << " [" << join(got.reasons, "; ")
                      << "] want [" << join(want, "; ") << "]\n";
        }
        ++g_checks;
    }
}

TEST(parity_games) {
    for (auto& g : golden().get("games").elements()) CHECK_EQ(classifyGame({jstr(g, "text")}), jstr(g, "game"));
}

// ── store / intelligence ────────────────────────────────────────
TEST(store_schema_and_views) {
    Store s(":memory:");
    CHECK_EQ(s.schemaVersion(), 7);
    auto src = s.registerSource("A", "https://a.example/", "web", "community", 65);
    long long id = s.insertCode("BTVJZBZ74W", "Global", "", "READY", 80, "", "codm");
    s.addSighting(id, src.id, "https://a.example/", "[x] BTVJZBZ74W - Skin", "active");
    s.addSighting(id, src.id, "https://a.example/", "[x] BTVJZBZ74W - Skin", "expired");  // upsert
    CHECK_EQ(s.reportCounts(id).expired, 1);
    CHECK_EQ(s.reportCounts(id).active, 0);
    CHECK_EQ(s.getCodeByText("btvjzbz74w")->id, id);
    CHECK_EQ(s.viewCounts()["ready"], 1);
    CHECK_EQ(s.codesView("ready").front().snippet, std::string("[x] BTVJZBZ74W - Skin"));
    s.setState("k", "v");
    CHECK_EQ(s.getState("k"), std::string("v"));
    long long x = s.insertCode("XBLX3-HN7X7-7NAH7", "Unknown", "", "VALIDATED", 0, "", "bo7");
    CHECK_EQ(s.getCodeByText("XBLX3HN7X77NAH7")->id, x);
    bool threw = false;
    try { s.updateCode(id, {{"code; DROP TABLE codes", "x"}}); } catch (...) { threw = true; }
    CHECK(threw);
}

TEST(intelligence_statuses) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    auto a = e.store.registerSource("Site A", "https://a.example/", "web", "unverified", 35);
    auto b = e.store.registerSource("Site B", "https://b.example/", "web", "unverified", 35);
    // first read of a page = baseline: recorded, not "new"
    auto f1 = e.intel.processDocument(Document::fromText("CODM redeem codes\nBTVJZBZ74W - Skin", "https://a.example/"), a);
    CHECK_EQ(f1.size(), size_t(1));
    CHECK(!f1.front().isNew);
    CHECK_EQ(e.store.getCodeByText("BTVJZBZ74W")->status, std::string("VALIDATED"));  // one low-trust site: 35 + 5 < 60
    // second site lists the same official-format code → READY
    a = *e.store.getSource("https://a.example/");
    e.store.updateSourceHealth("https://b.example/", true, "OK");
    b = *e.store.getSource("https://b.example/");
    auto f2 = e.intel.processDocument(Document::fromText("CODM redeem codes\nBTVJZBZ74W - Skin\nCODM7N6T1AW new", "https://b.example/"), b);
    CHECK_EQ(e.store.getCodeByText("BTVJZBZ74W")->status, std::string("READY"));
    CHECK(f2.size() == 2 && f2[1].isNew);
    // a site moves it to the expired list → LIKELY_EXPIRED
    e.intel.processDocument(Document::fromText("Expired codes\nBTVJZBZ74W", "https://a.example/", "Expired codes"), a);
    CHECK_EQ(e.store.getCodeByText("BTVJZBZ74W")->status, std::string("LIKELY_EXPIRED"));
    // junk is never stored; manual entry works
    auto [mid, why] = e.intel.addManualCode("callofduty", "Unknown", "", "codm");
    CHECK_EQ(mid, 0LL);
    auto [mid2, why2] = e.intel.addManualCode("H7K2M9P4Q1Z", "Unknown", "seen on stream", "codm");
    CHECK(mid2 > 0);
}

TEST(checker_verdicts_and_rewards) {
    Store s(":memory:");
    auto src = s.registerSource("A", "https://a.example/", "web", "community", 65);
    long long id = s.insertCode("BTVJZBZ74W", "Global", "", "READY", 80, "", "codm");
    s.updateCode(id, {}, {{"fresh", 1}});
    s.addSighting(id, src.id, "https://a.example/", "[Active] BTVJZBZ74W (New!) - Bathysphere Killer Whale", "active");
    auto row = *s.getCode(id);
    Verdict v = assessCode(s, row);
    CHECK_EQ(v.verdict, std::string("LIKELY WORKS"));
    CHECK_EQ(v.score, 85);  // 50 + 10 (1 site) + 15 (new today) + 10 (marked new)
    CHECK_EQ(rewardFrom("[Active codes] BTVJZBZ74W - Bathysphere Killer Whale", "BTVJZBZ74W"), std::string("Bathysphere Killer Whale"));
    CHECK_EQ(rewardFrom("[x] BTVJZBZ74W (Garena version) - Nomad skin. More text here", "BTVJZBZ74W"), std::string("Nomad skin"));
    CHECK_EQ(rewardFrom("[x] The code BTVJZBZ74W is almost gone", "BTVJZBZ74W"), std::string(""));
    s.setStatus(id, "LIMIT_REACHED");
    CHECK_EQ(assessCode(s, *s.getCode(id)).verdict, std::string("DEAD · CLAIMS USED UP"));
}

TEST(check_report_csv) {
    Store s(":memory:");
    auto src = s.registerSource("A", "https://a.example/", "web", "community", 65);
    long long a = s.insertCode("BTVJZBZ74W", "Global", "", "READY", 80, "", "codm");
    long long b = s.insertCode("BTWAZBZQ7K", "Global", "", "EXPIRED", 0, "", "codm");
    s.addSighting(a, src.id, "https://a.example/", "[x] BTVJZBZ74W - Skin, \"rare\"", "active");
    s.updateCode(a, {}, {{"fresh", 1}});
    checkAll(s);
    auto all = reportRows(s);
    CHECK(all.size() == 2 && all[0].code == "BTVJZBZ74W");
    CHECK_EQ(reportRows(s, false).size(), size_t(1));
    std::string csv = toCsv(all);
    CHECK(startsWith(csv, "\xEF\xBB\xBF" "code,game"));
    CHECK(icontains(csv, "BTWAZBZQ7K,codm,Global,,EXPIRED,DEAD · EXPIRED,0,"));
    CHECK(icontains(csv, ",\"Listed as working"));  // reasons contain commas → quoted
    (void)b;
}

// ── HTML / feeds / robots ───────────────────────────────────────
TEST(html_document_structure) {
    std::string html = R"(<html><head><title>T</title><meta name="description" content="Redeem codes list"></head>
      <body><nav>NAVCODE12345</nav><h1>COD Mobile codes</h1><p>Intro &amp; more<br>second line</p>
      <h2>Expired codes</h2><ul><li>BTVJZBZ74W - old</li></ul><table><tr><td>A</td><td>B</td></tr></table>
      <script id="__NEXT_DATA__" type="application/json">{"props":{"promoCode":"CODMGBRCW9H2A"}}</script></body></html>)";
    Document d = htmlToDocument(html, "https://x.example/");
    CHECK_EQ(d.title, std::string("COD Mobile codes"));
    std::vector<std::string> lines;
    for (auto& b : d.blocks) lines.push_back("[" + b.heading + "] " + b.text);
    CHECK_EQ(join(lines, "\n"), std::string("[COD Mobile codes] Redeem codes list\n[COD Mobile codes] Intro & more\n"
                                            "[COD Mobile codes] second line\n[Expired codes] BTVJZBZ74W - old\n[Expired codes] A B\n"
                                            "[COD Mobile codes (page data)] CODMGBRCW9H2A"));
    auto c = extractFromDocument(d);
    CHECK(c.size() == 2 && c[0].report == "expired" && c[1].code == "CODMGBRCW9H2A");
}

TEST(feeds_and_robots) {
    std::string rss = R"(<?xml version="1.0"?><rss><channel><title>x</title><item><title>New CODM codes &amp; rewards</title>
      <link>https://www.google.com/url?url=https%3A%2F%2Fsite.com%2Fcodes</link>
      <description><![CDATA[<b>BTVJZBZ74W</b> redeem now]]></description><pubDate>Mon, 23 Nov 2026</pubDate></item></channel></rss>)";
    auto items = parseFeedItems(rss);
    CHECK_EQ(items.size(), size_t(1));
    CHECK_EQ(items[0].title, std::string("New CODM codes & rewards"));
    CHECK_EQ(realResultUrl(items[0].link), std::string("https://site.com/codes"));
    CHECK_EQ(items[0].summary, std::string("<b>BTVJZBZ74W</b> redeem now"));
    std::string atom = R"(<feed xmlns="http://www.w3.org/2005/Atom"><entry><title>Video</title><link rel="alternate" href="https://youtu.be/x"/>
      <media:group><media:description>Code CODM7N6T1AW redeem</media:description></media:group></entry></feed>)";
    auto a = parseFeedItems(atom);
    CHECK(a.size() == 1 && a[0].link == "https://youtu.be/x" && a[0].summary == "Code CODM7N6T1AW redeem");
    Robots r("User-agent: *\nDisallow: /private\nAllow: /private/ok\n\nUser-agent: OtherBot\nDisallow: /", "CODRedemptionHunter/6.0");
    CHECK(r.allows("https://x.com/codes"));
    CHECK(!r.allows("https://x.com/private/page"));
    CHECK(r.allows("https://x.com/private/ok/1"));
    Robots mine("User-agent: codredemptionhunter\nDisallow: /\nUser-agent: *\nDisallow:", "CODRedemptionHunter/6.0");
    CHECK(!mine.allows("https://x.com/a"));
}

// ── discovery with a fake network ───────────────────────────────
static const char* kPage = "<html><body><h1>CODM redeem codes</h1><h2>Active codes</h2><ul>%s</ul></body></html>";
static std::string page(const std::vector<std::string>& lis) {
    std::string items;
    for (auto& l : lis) items += "<li>" + l + "</li>";
    std::string p = kPage;
    return replaceAll(p, "%s", items);
}

TEST(discovery_web_cycle) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    useSources(e.config, R"([{"name":"Site A","type":"web","url":"https://a.example/codes","trust":"established_publisher","enabled":true,"game":"codm"},
                            {"name":"Blocked","type":"web","url":"https://b.example/codes","trust":"community","enabled":true},
                            {"name":"Plain http","type":"web","url":"http://c.example/","trust":"community","enabled":true},
                            {"name":"Off","type":"web","url":"https://d.example/","trust":"community","enabled":false}])");
    e.discovery.sleeper = [](double) {};
    http.page("https://a.example/robots.txt", "User-agent: *\nDisallow: /private");
    http.page("https://b.example/robots.txt", "User-agent: *\nDisallow: /codes");
    http.page("https://a.example/codes", page({"BTVJZBZ74W - Skin A", "BTWAZBZQ7K - Skin B", "BTWCZBZQ9K - Skin C"}), 200, {{"etag", "\"v1\""}});
    auto r1 = e.runCycle(nullptr, true);
    CHECK_EQ(r1.scan.sources, 2);  // a, b (robots skip still counts); c (http) is an error
    CHECK_EQ(r1.scan.errors, 1);
    CHECK(r1.scan.newCodes.empty());  // baseline read
    bool robotsSkip = false;
    for (auto& l : e.store.recentScans(20)) if (l.name == "Blocked" && l.status == "SKIPPED" && icontains(l.details, "robots.txt")) robotsSkip = true;
    CHECK(robotsSkip);
    // second read sends the ETag; 304 → nothing changes
    http.page("https://a.example/codes", "", 304);
    e.runCycle(nullptr, true);
    bool sentEtag = false;
    for (auto& rq : http.sent) if (rq.url == "https://a.example/codes" && rq.headers.count("If-None-Match")) sentEtag = true;
    CHECK(sentEtag);
    // a new code appears, one is taken down
    http.page("https://a.example/codes", page({"BTVJZBZ74W - Skin A", "BTWAZBZQ7K - Skin B", "BTXDZBZR2M - New skin"}));
    auto r3 = e.runCycle(nullptr, true);
    CHECK_EQ(join(r3.scan.newCodes, ","), std::string("BTXDZBZR2M"));
    CHECK_EQ(e.store.reportCounts(e.store.getCodeByText("BTWCZBZQ9K")->id).removed, 1);
    CHECK_EQ(e.store.getCodeByText("BTWCZBZQ9K")->status, std::string("LIKELY_EXPIRED"));
    // 429 → paused, then skipped while paused
    http.page("https://a.example/codes", "", 429, {{"retry-after", "3600"}});
    e.runCycle(nullptr, true);
    CHECK(startsWith(e.store.getSource("https://a.example/codes")->lastStatus, "BLOCKED"));
    size_t before = http.sent.size();
    e.runCycle(nullptr, true);
    bool refetched = false;
    for (size_t i = before; i < http.sent.size(); ++i) if (http.sent[i].url == "https://a.example/codes") refetched = true;
    CHECK(!refetched);
    // stop flag is honoured
    std::atomic<bool> stop{true};
    CHECK_EQ(e.runCycle(&stop, true).scan.sources, 0);
}

TEST(discovery_google_alerts_and_news) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    useSources(e.config, R"([{"name":"Alerts","type":"google_alerts","url":"https://www.google.com/alerts/feeds/1/2","trust":"unverified","enabled":true,"max_pages":2},
                            {"name":"News","type":"google_news","query":"CODM codes","enabled":true,"trust":"unverified"}])");
    e.discovery.sleeper = [](double) {};
    http.page("https://www.google.com/alerts/feeds/1/2", R"(<feed><entry><title>CODM codes</title>
        <link href="https://www.google.com/url?url=https%3A%2F%2Fblog.example%2Fcodm"/><content>Redeem BTVJZBZ74W today</content></entry></feed>)");
    http.page("https://blog.example/robots.txt", "");
    http.page("https://blog.example/codm", page({"CODM7N6T1AW - Charm"}), 200, {{"content-type", "text/html"}});
    http.page(googleNewsUrl("CODM codes"), "<rss><channel><item><title>New COD Mobile code</title><link>https://n.example/a</link><description>Redeem BTWAZBZQ7K now</description></item></channel></rss>");
    auto r = e.runCycle(nullptr, true);
    CHECK_EQ(r.scan.errors, 0);
    CHECK(e.store.getCodeByText("BTVJZBZ74W").has_value());
    CHECK(e.store.getCodeByText("CODM7N6T1AW").has_value());   // read from the visited result page
    CHECK(e.store.getCodeByText("BTWAZBZQ7K").has_value());    // from the news headline
    CHECK(e.store.getCodeByText("BTVJZBZ74W")->fresh);          // search results are never a baseline
}

// ── Discord with a fake network ─────────────────────────────────
TEST(discord_sync) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    useSources(e.config, R"([{"name":"Site A","type":"web","url":"https://a.example/codes","trust":"official","enabled":true,"game":"codm"}])");
    e.discovery.sleeper = [](double) {};
    const std::string hook = "https://discord.com/api/webhooks/123/abc-DEF";
    e.config.writeEnv({{"DISCORD_WEBHOOK_URL", hook}});
    std::vector<Json> posts;
    http.handler = [&](const HttpRequest& rq) {
        HttpResponse r;
        if (rq.method == "POST") { posts.push_back(Json::parse(rq.body)); r.status = 204; return r; }
        auto it = http.pages.find(rq.url);
        if (it != http.pages.end()) return it->second;
        r.status = 404;
        return r;
    };
    http.page("https://a.example/robots.txt", "");
    http.page("https://a.example/codes", page({"BTVJZBZ74W - Skin A"}));
    DiscordNotifier probe(e.config, e.store, http);
    probe.sleeper = [](double) {};
    e.runCycle(nullptr, true);  // connect: posts the current list once
    CHECK_EQ(posts.size(), size_t(1));
    CHECK(icontains(jstr(posts[0], "content"), "Current CODM codes"));
    CHECK_EQ(jstr(posts[0], "username"), std::string("CODM Redemption Hunter"));  // from the shipped config
    http.page("https://a.example/codes", page({"BTVJZBZ74W - Skin A", "BTWAZBZQ7K (New!) - Skin B"}));
    e.runCycle(nullptr, true);
    CHECK_EQ(posts.size(), size_t(2));
    CHECK(icontains(jstr(posts[1], "content"), "1 new CODM code"));
    CHECK_EQ(jstr(posts[1].get("embeds").elements()[0], "title"), std::string("🆕 New CODM code: BTWAZBZQ7K"));
    e.runCycle(nullptr, true);  // nothing new → nothing posted
    CHECK_EQ(posts.size(), size_t(2));
    // code moves to an expired list → one update card
    http.page("https://a.example/codes", "<h1>CODM redeem codes</h1><h2>Expired codes</h2><ul><li>BTWAZBZQ7K</li><li>BTVJZBZ74W</li></ul>");
    e.runCycle(nullptr, true);
    CHECK_EQ(posts.size(), size_t(3));
    CHECK(icontains(jstr(posts[2], "content"), "update"));
    CHECK(validWebhook(hook));
    CHECK(!validWebhook("https://example.com/api/webhooks/1/x"));
}

// ── protection / config ─────────────────────────────────────────
TEST(protection_rules) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    CHECK(!e.protection.binding.check().first);  // no UID yet
    e.config.writeEnv({{"CODM_UID", "1234567890123456789"}});
    CHECK(e.protection.binding.check().first);   // binds
    e.config.writeEnv({{"CODM_UID", "9999999999"}});
    CHECK(!e.protection.binding.check().first);  // different account refused
    CHECK(e.protection.binding.rebind());
    CHECK(e.protection.checkAllowed().first);
    e.protection.breaker.recordHttp(429, "120");
    CHECK(!e.protection.checkAllowed().first);
    e.protection.breaker.close();
    for (int i = 0; i < 3; ++i) e.protection.breaker.recordOutcome("UNKNOWN");
    CHECK(e.protection.breaker.state().open);
    e.protection.breaker.close();
    long long id = e.store.insertCode("BTVJZBZ74W", "Global", "", "READY", 90, "", "codm");
    for (int i = 0; i < 5; ++i) e.store.markSubmitted(e.store.startAttempt(id, e.protection.binding.hash(), "Global"));
    auto lim = e.protection.limiter.check();
    CHECK(!lim.first && icontains(lim.second, "Daily cap"));
    CHECK(e.protection.limiter.acquire());
    CHECK(!e.protection.limiter.acquire());
    e.protection.limiter.release();
}

TEST(config_env_and_defaults) {
    TempEnv env;
    {
        Config c(env.paths());
        c.writeEnv({{"ACCOUNT_REGION", "Garena"}, {"DISCORD_WEBHOOK_URL", "https://discord.com/api/webhooks/1/x\n"}});
        CHECK_EQ(c.env("ACCOUNT_REGION"), std::string("Garena"));
        CHECK_EQ(c.accountRegions().size(), size_t(1));
        std::string text = readFile(env.paths().env());
        CHECK(icontains(text, "# example"));                    // your comments survive
        CHECK(icontains(text, "# Discord webhook for new-code posts"));
        CHECK(!icontains(text, "x\n\n"));
        Json cfg = c.snapshot();
        cfg["runtime"]["theme"] = "light";
        cfg["sources"] = Json::array();
        cfg["version"] = "5.0.0";
        c.save(cfg);
    }
    Config again(env.paths());  // a "newer install": shipped sources come back, your theme stays
    CHECK_EQ(jstr(again.snapshot().get("runtime"), "theme"), std::string("light"));
    CHECK(again.snapshot().get("sources").size() > 10);
    CHECK_EQ(again.version(), jstr(Json::parse(readFile(kRoot / "data" / "config.json")), "version"));  // shipped version wins
    CHECK(again.pollIntervalSeconds() >= 120);
}


// ════════════════ stage 3: AI, research, screenshots, studio ════════════════
static const Json& golden2() {
    static Json g = Json::parse(readFile(kRoot / "tests" / "golden_research.json"));
    return g;
}

TEST(parity_research_extraction) {
    const Json& g = golden2();
    Document d{"https://a.example/m4", "Best Warzone loadouts", {}};
    auto add = [&](const char* h, const char* t) { d.blocks.push_back({h, t}); };
    add("Best M4 loadout", "Optic: Corio Eagleseye 2.5x"); add("Best M4 loadout", "Muzzle - FSS IS300");
    add("Best M4 loadout", "\xE2\x80\xA2 Barrel: Tempus 22.5\" Barrel"); add("Best M4 loadout", "Underbarrel | DR-6 Handstop");
    add("Best M4 loadout", "Magazine 45 Round Mag"); add("Best M4 loadout", "Optic: Slate Reflector");
    add("Best M4 loadout", "Barrel: Long Barrel"); add("Best M4 loadout", "Stock: Agile Stock");
    add("Best M4 loadout", "Perks: Double Time");
    add("Other guns", "Best Kilo 141 class setup"); add("Other guns", "Scope: 3x"); add("Other guns", "Ammo: 60 Round");
    add("Other guns", "Rear grip: Stippled"); add("Other guns", "Perk: Ghost");
    add("Ending", "This is a sentence about the M4 and stuff that is quite long and goes on and on for a while");
    Json got = buildsFromDocument(d, {"ISO Nightshade", "M4", "Kilo 141"});
    CHECK_EQ(got.dump(), g.get("builds").get("m4").dump());

    Document sd{"https://s.example/", "Best settings", {}};
    auto addS = [&](const char* h, const char* t) { sd.blocks.push_back({h, t}); };
    addS("Graphics settings", "Field of View: 110"); addS("Graphics settings", "Motion Blur - Off");
    addS("Graphics settings", "Film Grain: 0.00"); addS("Controller", "Aim Assist Type: Precision");
    addS("Controller", "Left Stick Min Input Deadzone (Left) - 3");
    addS("Controller", "Sensitivity is the thing you want to tune a lot over time ok");
    addS("Controller", "Response Curve Type: Dynamic");
    addS("Very long heading that is more than forty characters for sure", "Bloom = Off");
    addS("Audio", "Audio Mix: Headphones Bass Boost"); addS("x", "DPI 800");
    CHECK_EQ(settingsFromDocument(sd).dump(), g.get("settings").dump());
    for (const auto& [k, v] : g.get("slots").items()) CHECK_EQ(canonSlot(k), v.toString());
}

TEST(parity_research_catalog_links_crosscheck) {
    const Json& g = golden2();
    Json cfg = Json::parse(readFile(kRoot / "data" / "config.json"));
    Paths p{kRoot / "data", fs::temp_directory_path()};
    SourceCatalog cat(p);
    for (const auto& [key, want] : g.get("catalog").items()) {
        auto colon = key.find(':');
        auto got = cat.forGame(cfg, key.substr(0, colon), key.substr(colon + 1));
        std::vector<std::string> a, b;
        for (auto& e : got) a.push_back(e.url + (e.crawl ? " crawl" : "") + " p" + std::to_string(e.priority) + (e.gameSpecific ? " gs" : ""));
        for (auto& e : want.elements())
            b.push_back(jstr(e, "url") + (jbool(e, "crawl") ? " crawl" : "") + " p" + std::to_string(jinteger(e, "priority")) +
                        (jbool(e, "game_specific") ? " gs" : ""));
        compareLists("catalog " + key, a, b);
    }
    std::string html = R"(<html><body><a href="/mw4/best-loadouts">MW4 best loadouts</a><a href="https://other.com/mw4-loadout">x</a>
<a href="/login?mw4-build">login</a><a href="/tag/mw4">tag</a><a href="/news/bo6-meta-tier-list">Black Ops 6 tier list</a>
<a href="https://sub.site.gg/modern-warfare-4/weapon-guns">MW4 guns</a><a href="/mw4/best-loadouts#top">dup</a>
<a href="/meta">meta only</a><a href="/">home</a></body></html>)";
    auto list = [](const Json& j) { std::vector<std::string> v; for (auto& x : j.elements()) v.push_back(x.toString()); return v; };
    compareLists("links a", findLinks(html, "https://www.site.gg/", "mw4", "builds", 3), list(g.get("links").get("a")));
    compareLists("links b", findLinks(html, "https://www.site.gg/hub", "mw4", "builds", 5, true), list(g.get("links").get("b")));
    compareLists("links c", findLinks(html, "https://www.site.gg/", "bo6", "builds", 3), list(g.get("links").get("c")));

    Json found = Json::parse(R"([{"weapon":"M4","attachments":[["Optic","Corio"],["Muzzle","IS300"],["Barrel","Tempus"]],"url":"https://a.com/x","via":"text","perks":["Double Time"]},
 {"weapon":"m4","attachments":[["optic","corio"],["Muzzle","Other"],["Stock","Agile"]],"url":"https://www.b.com/y","via":"ai","class":"AR","perks":["Ghost","Double Time"]},
 {"weapon":"M4","attachments":[["Scope","Corio"],["Muzzle","Other"]],"url":"https://a.com/z","via":"image","image":"https://a.com/i.png"},
 {"weapon":"Kilo 141","attachments":[["Optic","3x"],["Ammo","60"]],"url":"https://c.com/","via":"text"},
 {"weapon":"X","attachments":[],"url":"https://c.com/"}])");
    Json curated = Json::parse(R"([{"weapon":"M4","attachments":[["Optic","Corio"],["Muzzle","IS300"]]}])");
    Json want = g.get("cross");
    Json got = crossCheck(found, curated);
    // Python writes curated_match None as null and missing class as ""; compare field by field
    CHECK_EQ(got.size(), want.size());
    for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        for (const char* k : {"weapon", "sites", "domains", "verified", "attachments", "perks", "equipment", "via", "sources"})
            CHECK_EQ(got.elements()[i].get(k).dump(), want.elements()[i].get(k).dump());
        CHECK_EQ(jstr(got.elements()[i], "class"), jstr(want.elements()[i], "class"));
        CHECK_EQ(jstr(got.elements()[i], "curated_match"), jstr(want.elements()[i], "curated_match"));
    }
    Json sf = Json::parse(R"([{"section":"View","setting":"Field of View","value":"110","url":"https://a.com/1"},{"section":"View","setting":"field of view","value":"105","url":"https://b.com/1","why":"wider"},
    {"section":"View","setting":"Field Of View","value":"110","url":"https://c.com/1"},{"section":"Graphics","setting":"Bloom","value":"Off","url":"https://a.com/1"},
    {"setting":"DPI","value":"800","url":"https://a.com/2"},{"setting":"","value":"x","url":"y"}])");
    CHECK_EQ(mergeSettings(sf).dump(), g.get("merge").dump());
}

TEST(util_urls_base64) {
    CHECK_EQ(resolveUrl("https://a.com/x/y/page", "../img/p.png"), std::string("https://a.com/x/img/p.png"));
    CHECK_EQ(resolveUrl("https://a.com/x/", "/b?c=1#frag"), std::string("https://a.com/b?c=1"));
    CHECK_EQ(resolveUrl("https://a.com/x/", "//cdn.b.com/i.jpg"), std::string("https://cdn.b.com/i.jpg"));
    CHECK_EQ(resolveUrl("https://a.com/x/", "javascript:void(0)"), std::string(""));
    CHECK_EQ(base64Encode("Man"), std::string("TWFu"));
    CHECK_EQ(base64Encode("Ma"), std::string("TWE="));
    CHECK_EQ(base64Encode("M"), std::string("TQ=="));
}

// AI with a fake network: request shapes for every wire format, streaming, errors
TEST(ai_client_wires_and_streaming) {
    FakeHttp http;
    std::vector<HttpRequest> seen;
    std::string reply;
    int status = 200;
    http.handler = [&](const HttpRequest& rq) { seen.push_back(rq); HttpResponse r; r.status = status; r.body = reply; return r; };
    AiClient none(http, "OpenAI", "", "", "");
    CHECK(icontains(none.problem(), "API key"));
    AiClient local(http, "Ollama (local)", "", "llama3", "");
    CHECK(local.ready());
    AiClient plainHttp(http, "Custom (OpenAI-compatible)", "k", "m", "http://example.com/v1");
    CHECK(icontains(plainHttp.problem(), "https"));

    AiClient oa(http, "OpenAI", "sk-1", "gpt-x");
    oa.sleeper = [](double) {};
    reply = R"({"choices":[{"message":{"content":"Hi there"}}]})";
    CHECK_EQ(oa.chat("hello", "sys", {{"image/png", "PNG"}}), std::string("Hi there"));
    Json body = Json::parse(seen.back().body);
    CHECK_EQ(seen.back().url, std::string("https://api.openai.com/v1/chat/completions"));
    CHECK_EQ(seen.back().headers.at("Authorization"), std::string("Bearer sk-1"));
    CHECK_EQ(jstr(body.get("messages").elements()[0], "role"), std::string("system"));
    CHECK(startsWith(jstr(body.get("messages").elements()[1].get("content").elements()[1].get("image_url"), "url"), "data:image/png;base64,UE5H"));

    AiClient an(http, "Anthropic (Claude)", "ak", "claude-x");
    reply = R"({"content":[{"type":"text","text":"Claude says hi"}]})";
    CHECK_EQ(an.chat("q", "sys"), std::string("Claude says hi"));
    CHECK_EQ(seen.back().headers.at("anthropic-version"), std::string("2023-06-01"));
    CHECK_EQ(jstr(Json::parse(seen.back().body), "system"), std::string("sys"));
    CHECK_EQ(an.nativeSearch(), std::string("Claude web search"));

    AiClient ge(http, "Google Gemini", "gk", "gemini-flash");
    reply = R"({"candidates":[{"content":{"parts":[{"text":"thinking","thought":true},{"text":"Gemini answer"}]}}]})";
    CHECK_EQ(ge.chat("q"), std::string("Gemini answer"));
    CHECK_EQ(seen.back().url, std::string("https://generativelanguage.googleapis.com/v1beta/models/gemini-flash:generateContent"));
    CHECK_EQ(jinteger(Json::parse(seen.back().body).get("generationConfig"), "maxOutputTokens"), 6000LL);

    // streaming (Anthropic SSE with a web search result)
    reply = "event: x\ndata: {\"type\":\"content_block_start\",\"content_block\":{\"type\":\"server_tool_use\"}}\n\n"
            "data: {\"type\":\"content_block_start\",\"content_block\":{\"type\":\"web_search_tool_result\",\"content\":[{\"url\":\"https://src.example/a\",\"title\":\"A\"}]}}\n\n"
            "data: {\"type\":\"content_block_delta\",\"delta\":{\"text\":\"Hel\"}}\n"
            "data: {\"type\":\"content_block_delta\",\"delta\":{\"text\":\"lo\"}}\n";
    std::string typed, status2;
    std::vector<std::string> srcs;
    std::string full = an.chatStream("q", [&](const std::string& t) { typed += t + "|"; }, "", {}, 100, {}, true,
                                     [&](const std::string& u, const std::string&) { srcs.push_back(u); },
                                     [&](const std::string& m) { status2 = m; });
    CHECK_EQ(full, std::string("Hello"));
    CHECK_EQ(typed, std::string("Hel|lo|"));
    CHECK(srcs.size() == 1 && srcs[0] == "https://src.example/a");
    CHECK(icontains(status2, "searching"));
    CHECK(Json::parse(seen.back().body).get("tools").size() == 1);
    // OpenAI stream with [DONE] and url_citation
    reply = "data: {\"choices\":[{\"delta\":{\"content\":\"A\"}}]}\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"B\",\"annotations\":[{\"url_citation\":{\"url\":\"https://x.y/z\"}}]}}]}\n\ndata: [DONE]\n\ndata: {\"choices\":[{\"delta\":{\"content\":\"IGNORED\"}}]}\n";
    srcs.clear();
    CHECK_EQ(oa.chatStream("q", [](const std::string&) {}, "", {}, 100, {}, false,
                           [&](const std::string& u, const std::string&) { srcs.push_back(u); }), std::string("AB"));
    CHECK_EQ(srcs.size(), size_t(1));
    // errors
    status = 401;
    reply = R"({"error":{"message":"bad key"}})";
    bool threw = false;
    try { oa.chat("q"); } catch (const AiError& e) { threw = icontains(e.what(), "rejected the API key"); }
    CHECK(threw);
    status = 200;
    {
        Json msg = Json::object();
        msg["content"] = "Sure! ```json\n{\"builds\": [{\"weapon\": \"M4\"}]}\n``` hope it helps";
        Json choice = Json::object();
        choice["message"] = msg;
        Json wrap = Json::object();
        wrap["choices"] = Json::array();
        wrap["choices"].push(choice);
        reply = wrap.dump();
    }
    CHECK_EQ(jstr(oa.chatJson("q").get("builds").elements()[0], "weapon"), std::string("M4"));
    CHECK_EQ(suggestModel({"text-embedding-3", "gpt-5", "gpt-5-mini", "whisper-1"}), std::string("gpt-5-mini"));
}

TEST(screenshot_library) {
    TempEnv env;
    Paths p = env.paths();
    fs::create_directories(p.dataDir);
    ScreenshotLibrary lib(p);
    std::string png(6000, 'x');
    Json m = lib.save("mw4", png, "image/png", "M4", "https://www.site.gg/guide", "https://site.gg/i.png", "M4 loadout");
    CHECK_EQ(jstr(m, "site"), std::string("site.gg"));
    CHECK(fs::exists(jstr(m, "file")));
    Json again = lib.save("mw4", png, "image/png", "", "", "", "");  // same bytes → same entry, credit kept
    CHECK_EQ(jstr(again, "source_page"), std::string("https://www.site.gg/guide"));
    CHECK_EQ(lib.listing("mw4").size(), size_t(1));
    CHECK_EQ(lib.listing("mw4", "m4").size(), size_t(1));
    CHECK_EQ(lib.listing("mw4", "AK").size(), size_t(0));
    fs::path mine = env.dir / "mine.jpg";
    std::ofstream(mine, std::ios::binary) << std::string(7000, 'y');
    Json own = lib.addFile("mw4", mine, "AK-47");
    CHECK_EQ(jstr(own, "added_by"), std::string("you"));
    // pruning removes web screenshots only
    lib.save("mw4", std::string(9000, 'z'), "image/jpeg");
    lib.prune(0.0);
    auto left = lib.listing("mw4");
    CHECK(left.size() == 1 && jstr(left[0], "added_by") == "you");
    bool threw = false;
    try { lib.save("mw4", png, "image/bmp"); } catch (...) { threw = true; }
    CHECK(threw);
}

TEST(meta_and_studio) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    // shipped meta.json lives next to config.json
    fs::copy_file(kRoot / "data" / "meta.json", env.dir / "app" / "meta.json");
    MetaData meta(e.config.paths());
    Json mw4 = meta.forGame("mw4");
    CHECK(mw4.get("builds").size() > 0);
    CHECK(mw4.get("settings").get("sections").size() > 0);
    std::string text = buildText(mw4.get("builds").elements()[0], "MW4");
    CHECK(icontains(text, "Source:"));
    CHECK(!meta.knownWeapons().empty());

    Json items = Json::array();
    for (size_t i = 0; i < 12 && i < mw4.get("builds").size(); ++i) items.push(mw4.get("builds").elements()[i]);
    while (items.size() < 12) items.push(mw4.get("builds").elements()[0]);
    auto msgs = studioMessages(e.config, e.store, "builds", "mw4", items, "Best MW4 builds", false);
    CHECK_EQ(msgs.size(), size_t(2));  // 10 cards per message
    CHECK_EQ(jstr(msgs[0].payload, "content"), std::string("Best MW4 builds"));
    CHECK_EQ(msgs[0].payload.get("embeds").size(), size_t(10));
    CHECK(icontains(studioPreview(msgs), "message 1 of 2"));
    // screenshots: your own are uploaded, site ones linked with credit
    ScreenshotLibrary lib(e.config.paths());
    Json own = lib.save("mw4", std::string(6000, 'a'), "image/png", "M4", "", "", "", "you");
    Json web = lib.save("mw4", std::string(6000, 'b'), "image/png", "M4", "https://guide.example/m4", "https://guide.example/m4.png");
    Json shots = Json::array();
    shots.push(own);
    shots.push(web);
    auto sm = studioMessages(e.config, e.store, "shots", "mw4", shots, "", false);
    CHECK_EQ(sm[0].files.size(), size_t(1));
    CHECK(startsWith(jstr(sm[0].payload.get("embeds").elements()[0].get("image"), "url"), "attachment://"));
    CHECK_EQ(jstr(sm[0].payload.get("embeds").elements()[1].get("image"), "url"), std::string("https://guide.example/m4.png"));
    CHECK(icontains(jstr(sm[0].payload.get("embeds").elements()[1].get("footer"), "text"), "credit: guide.example"));
    auto [body, ctype] = multipartBody(sm[0].payload, sm[0].files);
    CHECK(startsWith(ctype, "multipart/form-data; boundary="));
    CHECK(icontains(body, "name=\"payload_json\"") && icontains(body, "name=\"files[0]\""));
    std::vector<HttpRequest> posted;
    http.handler = [&](const HttpRequest& rq) { posted.push_back(rq); HttpResponse r; r.status = 204; return r; };
    auto [ok, msg] = studioSend(e.config, e.store, http, "https://discord.com/api/webhooks/1/abc", sm, "shots");
    CHECK(ok);
    CHECK(posted.size() == 1 && startsWith(posted[0].contentType, "multipart/form-data"));
    CHECK(!studioSend(e.config, e.store, http, "https://example.com", sm, "shots").first);
}

TEST(helper_ask_free_lookup) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    fs::copy_file(kRoot / "data" / "meta.json", env.dir / "app" / "meta.json");
    fs::copy_file(kRoot / "data" / "research_sources.json", env.dir / "app" / "research_sources.json");
    Json cfg = e.config.snapshot();
    cfg["research"]["use_catalog"] = false;
    cfg["research"]["pages"]["warzone"] = Json::parse(R"(["https://guide.example/wz"])");
    e.config.save(cfg);
    Helper helper(e.config, e.store, http);
    helper.sleeper = [](double) {};
    std::string sentPrompt;
    http.handler = [&](const HttpRequest& rq) {
        HttpResponse r;
        r.status = 200;
        if (icontains(rq.url, "robots.txt")) { r.body = ""; return r; }
        if (icontains(rq.url, "news.google.com")) {
            r.body = "<rss><channel><item><title>Warzone patch</title><link>https://news.example/p</link><description>Nerfs</description></item></channel></rss>";
            return r;
        }
        if (icontains(rq.url, "wikipedia.org")) { r.status = 404; return r; }
        if (rq.url == "https://guide.example/wz") { r.body = "<h1>Best Warzone loadouts</h1><p>Optic: Corio</p>"; return r; }
        if (icontains(rq.url, "/chat/completions")) {
            sentPrompt = rq.body;
            r.body = "data: {\"choices\":[{\"delta\":{\"content\":\"Use the M4 [1]\"}}]}\n\ndata: [DONE]\n";
            return r;
        }
        r.status = 404;
        return r;
    };
    AiClient ai(http, "Groq", "k", "llama");  // no native web search → free lookup
    std::vector<std::string> kinds;
    std::string typed;
    Json res = helper.ask(ai, "best warzone loadout?", "warzone", true, {}, {}, [&](const std::string& k, const Json& d) {
        kinds.push_back(k);
        if (k == "text") typed += d.toString();
    });
    CHECK_EQ(jstr(res, "answer"), std::string("Use the M4 [1]"));
    CHECK_EQ(typed, std::string("Use the M4 [1]"));
    CHECK_EQ(jstr(res, "search"), std::string("free lookup"));
    CHECK(res.get("sources").size() == 2);  // the guide page + the news item
    CHECK(icontains(sentPrompt, "guide.example") && icontains(sentPrompt, "Warzone patch") && icontains(sentPrompt, "Curated builds"));
    CHECK(std::find(kinds.begin(), kinds.end(), "source") != kinds.end());
    CHECK(!helper.isCod("how tall is the eiffel tower", "warzone"));
    Json turns = Json::array();
    turns.push(Json::parse(R"({"role":"user","text":"hi"})"));
    helper.saveChat(turns);
    CHECK_EQ(helper.loadChat().size(), size_t(1));
    helper.saveAnswer("q", res, "warzone");
    CHECK_EQ(helper.savedAnswers().size(), size_t(1));
}

// ════════════════ stage 4: stats, patches, community, playstyle, push, languages ════════════════
static const Json& golden4() {
    static Json g = Json::parse(readFile(kRoot / "tests" / "golden_stage4.json"));
    return g;
}
// Python writes 34.0 for floats; compare JSON numerically
static bool sameJson(const Json& a, const Json& b) {
    if (a.isNumber() && b.isNumber()) return std::fabs(a.toDouble() - b.toDouble()) < 1e-9;
    if (a.type() != b.type()) return false;
    if (a.isArray()) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (!sameJson(a.elements()[i], b.elements()[i])) return false;
        return true;
    }
    if (a.isObject()) {
        if (a.size() != b.size()) return false;
        for (const auto& [k, v] : a.items()) if (!b.contains(k) || !sameJson(v, b.get(k))) return false;
        return true;
    }
    return a == b;
}
#define CHECK_JSON(a, b) do { ++g_checks; if (!sameJson((a), (b))) { ++g_failed; \
    std::cerr << "  FAIL " << __LINE__ << "\n    got:  " << (a).dump() << "\n    want: " << (b).dump() << "\n"; } } while (0)

TEST(parity_patch_notes) {
    std::vector<std::string> weapons = {"M4", "Kilo 141", "VEL 46", "AK-47", "MP5"};
    for (const auto& c : golden4().get("changes").elements()) CHECK_JSON(weaponChanges(jstr(c, "text"), weapons), c.get("out"));
    for (const auto& [in, want] : golden4().get("dates").items()) {
        if (in == "2026-11-20") { CHECK_EQ(parsePatchDate(in), std::string("2026-11-20T00:00:00+00:00")); continue; }  // UTC, not local time
        CHECK_EQ(parsePatchDate(in), want.toString());
    }
}

TEST(parity_weapon_stats) {
    Document doc{"https://s.example/", "Stats", {}};
    auto add = [&](const char* h, const char* t) { doc.blocks.push_back({h, t}); };
    add("M4 stats", "Fire rate: 810 RPM"); add("M4 stats", "ADS time 220 ms"); add("M4 stats", "Bullet velocity: 780 m/s");
    add("M4 stats", "Magazine 30 rounds"); add("M4 stats", "0-25m: 34 damage"); add("M4 stats", "25 \xE2\x80\x93 45m 28");
    add("M4 stats", "45-\xE2\x88\x9Em: 24"); add("Kilo 141", "Kilo 141 rate of fire 750"); add("Kilo 141", "Sprint-to-fire: 180ms");
    add("Kilo 141", "Mag size 40 m away"); add("Kilo 141", "ammo 45"); add("Other", "just text");
    std::vector<std::string> weapons = {"M4", "Kilo 141", "VEL 46", "AK-47", "MP5"};
    CHECK_JSON(statsFromDocument(doc, weapons), golden4().get("stats"));
    CHECK_JSON(cleanStats(Json::parse(R"({"weapon":" M4 ","rpm":"5000","ads_ms":220.7,"reload_s":"2.345","mag":0,"damage":[[0,34],[25,"28.25"],[600,10],[25,29],["x",1]]})")),
               golden4().get("clean"));
    Json row = Json::parse(R"({"rpm":800,"damage":[[0,34],[25,28],[45,24]]})");
    CHECK_JSON(ttkOf(row, 100), golden4().get("ttk").get("t100"));
    CHECK_JSON(ttkOf(row, 250), golden4().get("ttk").get("t250"));
    Json curve = Json::array();
    for (auto& [m, t] : ttkCurve(row, 150)) { Json p = Json::array(); p.push(m); p.push(t); curve.push(p); }
    CHECK_JSON(curve, golden4().get("ttk").get("curve"));

    // storing, change history and the cross-site table
    Store s(":memory:");
    Json a = Json::parse(R"({"weapon":"M4","rpm":800,"damage":[[0,34],[30,28]]})");
    CHECK_EQ(recordStats(s, "mw4", a, "site-a").size(), size_t(0));
    CHECK_EQ(recordStats(s, "mw4", Json::parse(R"({"weapon":"M4","rpm":820})"), "site-b").size(), size_t(0));
    Json ch = recordStats(s, "mw4", Json::parse(R"({"weapon":"M4","rpm":780})"), "site-a");
    CHECK(ch.size() == 1 && ch.elements()[0].elements()[0].toString() == "rpm");
    CHECK_EQ(s.statChanges("mw4").size(), size_t(1));
    Json t = statsTable(s, "mw4", 100);
    CHECK_EQ(t.size(), size_t(1));
    CHECK_EQ(jnum(t.elements()[0], "rpm"), 800.0);  // median of 780 and 820
    CHECK_EQ(jinteger(t.elements()[0], "stk_close"), 3LL);
    CHECK_EQ(jinteger(t.elements()[0], "ttk_close"), 150LL);
}

TEST(parity_community_and_playstyle) {
    const Json& g = golden4();
    const double want[] = {0, 0, 0, 0, 0};
    (void)want;
    int pairs[][2] = {{0, 0}, {1, 0}, {50, 1}, {10, 10}, {3, 7}};
    for (size_t i = 0; i < 5; ++i) CHECK(std::fabs(wilson(pairs[i][0], pairs[i][1]) - g.get("wilson").elements()[i].toDouble()) < 1e-9);
    Json b = Json::parse(R"({"weapon":"M4","class":"AR","mode":"warzone","playstyle":"slayer","attachments":[["Optic","Corio"],["Muzzle","IS300"]],"perks":["Double Time"]})");
    auto [title, body] = Community::buildIssue(b, "warzone", "My favourite");
    CHECK_EQ(title, jstr(g, "issue_title"));
    Json issue = Json::object();
    issue["number"] = 7;
    issue["html_url"] = "https://github.com/o/r/issues/7";
    issue["title"] = title;
    issue["body"] = body;
    issue["user"] = Json::parse(R"({"login":"alice"})");
    issue["reactions"] = Json::parse(R"({"+1":5,"-1":1})");
    issue["labels"] = Json::parse(R"([{"name":"verified"}])");
    issue["created_at"] = "2026-11-20T00:00:00Z";
    issue["comments"] = 2;
    Json p = Community::parseIssue(issue);
    p.erase("date");
    Json wantP = g.get("parsed");
    for (const auto& [k, v] : wantP.items()) {
        if (k == "avatar") continue;
        ++g_checks;
        if (!sameJson(p.get(k), v)) { ++g_failed; std::cerr << "  FAIL parsed." << k << ": " << p.get(k).dump() << " vs " << v.dump() << "\n"; }
    }
    Json pr = Json::object();
    pr["pull_request"] = Json::object();
    CHECK(Community::parseIssue(pr).isNull());
    for (const auto& [text, want2] : g.get("classes").items()) {
        CHECK_EQ(weaponClassOf(text), want2.elements()[0].toString());
        CHECK_EQ(weaponRangeOf(text), want2.elements()[1].toString());
    }
}

TEST(community_github_fake) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    Json cfg = e.config.snapshot();
    cfg["community"]["repo"] = "https://github.com/someone/cod-builds/";
    cfg["community"]["following"] = Json::parse(R"(["alice"])");
    e.config.save(cfg);
    Community c(e.config, http);
    CHECK_EQ(c.repo(), std::string("someone/cod-builds"));
    Json b = Json::parse(R"({"weapon":"M4","attachments":[["Optic","Corio"]]})");
    auto [title, body] = Community::buildIssue(b, "mw4");
    std::string url = c.submitUrl(b, "mw4");
    CHECK(startsWith(url, "https://github.com/someone/cod-builds/issues/new?title=%5BModern%20Warfare%204%5D%20M4&body="));
    int calls = 0;
    http.handler = [&](const HttpRequest& rq) {
        HttpResponse r;
        ++calls;
        if (icontains(rq.url, "/issues?state=open")) {
            if (rq.headers.count("If-None-Match")) { r.status = 304; return r; }
            Json issues = Json::array();
            Json i = Json::object();
            i["number"] = 1; i["title"] = title; i["body"] = body; i["html_url"] = "https://github.com/x/1";
            i["user"] = Json::parse(R"({"login":"alice"})");
            i["reactions"] = Json::parse(R"({"+1":2,"-1":0})");
            i["created_at"] = "2026-11-20T00:00:00Z";
            issues.push(i);
            Json noise = Json::object();
            noise["number"] = 2; noise["body"] = "just a question";
            issues.push(noise);
            r.status = 200;
            r.body = issues.dump();
            r.headers["etag"] = "\"abc\"";
            return r;
        }
        r.status = 404;
        return r;
    };
    Json builds = c.refresh(true);
    CHECK_EQ(builds.size(), size_t(1));
    CHECK_EQ(jstr(builds.elements()[0], "author"), std::string("alice"));
    CHECK_EQ(c.buildsFor("mw4").size(), size_t(1));
    CHECK_EQ(c.buildsFor("bo7").size(), size_t(0));
    Json again = c.refresh(true);  // forced: no ETag sent, still works
    CHECK_EQ(again.size(), size_t(1));
    CHECK_EQ(c.newFromFollowed(e.store, builds).size(), size_t(0));  // first look just remembers
    Json more = builds;
    Json nb = builds.elements()[0];
    nb["number"] = 9;
    more.push(nb);
    CHECK_EQ(c.newFromFollowed(e.store, more).size(), size_t(1));
    Json creators = c.creators(builds);
    CHECK(creators.size() == 1 && jbool(creators.elements()[0], "following"));
    bool threw = false;
    try { c.vote(1, true); } catch (const CommunityError&) { threw = true; }  // no token
    CHECK(threw);
}

TEST(recommend_and_tracking) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    Json cands = Json::parse(R"([
      {"weapon":"MP5","class":"SMG","role":"close range rush","tier":"S","origin":"curated","as_of":"2026-11-01"},
      {"weapon":"Sniper X","class":"Sniper","role":"long range","tier":"A","origin":"curated","as_of":"2026-11-01"},
      {"weapon":"MP5","class":"SMG","role":"","tier":"","origin":"researched","verified":true,"sites":3,"as_of":"2026-11-01"},
      {"weapon":"M4","class":"Assault rifle","role":"mid range","tier":"","origin":"community","up":10,"down":0,"score":0.72,"playstyle":"rusher","as_of":"2026-11-01"}])");
    Json prof = Json::parse(R"({"input":"controller","playstyle":"rusher","mode":"multiplayer"})");
    Json recs = recommend(e.config, e.store, cands, "mw4", 8, prof);
    CHECK_EQ(recs.size(), size_t(3));  // one per weapon
    CHECK_EQ(jstr(recs.elements()[0], "weapon"), std::string("MP5"));
    // a patch nerfing the MP5 after the build's date drops it
    e.store.insertPatch("mw4", "Season 1 Reloaded", "https://p.example/1", "2026-11-10T00:00:00+00:00",
                        R"([{"weapon":"MP5","direction":"nerf","detail":[]}])", true);
    Json fr = buildFreshness(e.store, "mw4", "MP5", "2026-11-01");
    CHECK_EQ(jstr(fr, "state"), std::string("outdated"));
    CHECK_EQ(jstr(buildFreshness(e.store, "mw4", "M4", "2026-11-01"), "state"), std::string("check"));
    recs = recommend(e.config, e.store, cands, "mw4", 8, prof);
    CHECK_EQ(jstr(recs.elements()[0], "weapon"), std::string("M4"));
    // your results
    for (int i = 0; i < 3; ++i) logMatch(e.store, "mw4", "M4", 20, 5, true);
    logMatch(e.store, "mw4", "MP5", 5, 10, false);
    Json perf = performance(e.store, "mw4");
    CHECK_EQ(jinteger(perf.get(buildKey("mw4", "M4")), "matches"), 3LL);
    CHECK_EQ(jnum(perf.get(buildKey("mw4", "M4")), "kd"), 4.0);
    CHECK_EQ(jinteger(perf.get("*"), "win_rate"), 75LL);
}

TEST(push_and_languages) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    CHECK(!pushSend(e.config, http, "t", "m").first);  // no topic
    std::string topic = newPushTopic();
    CHECK(startsWith(topic, "codhunter-") && topic.size() == 26);
    e.config.writeEnv({{"NTFY_TOPIC", topic}});
    std::vector<HttpRequest> sent;
    http.handler = [&](const HttpRequest& rq) { sent.push_back(rq); HttpResponse r; r.status = 200; return r; };
    auto [ok, msg] = pushSend(e.config, http, "🎯 2 new codes", "BTVJZBZ74W", "https://x.y/", "dart");
    CHECK(ok);
    CHECK_EQ(sent.back().url, "https://ntfy.sh/" + topic);
    CHECK_EQ(sent.back().headers.at("Title"), std::string("2 new codes"));  // header kept ASCII-safe
    CHECK_EQ(pushSubscribeUrl(e.config), "https://ntfy.sh/" + topic);
    Json cfg = e.config.snapshot();
    cfg["push"]["events"]["patch"] = false;
    e.config.save(cfg);
    CHECK(!pushSend(e.config, http, "t", "m", "", "x", 3, "patch").first);

    fs::create_directories(env.dir / "app" / "i18n");
    std::ofstream(env.dir / "app" / "i18n" / "es.json") << R"({"_language":"es","Settings":"Ajustes"})";
    std::ofstream(env.dir / "app" / "i18n" / "strings.json") << R"({"strings":["Settings","Scan now"]})";
    Translator tr(e.config.paths());
    tr.load("es");
    CHECK_EQ(tr.t("Settings"), std::string("Ajustes"));
    CHECK_EQ(tr.t("Scan now"), std::string("Scan now"));
    CHECK_EQ(tr.missing().size(), size_t(1));
    auto av = tr.available();
    CHECK(av.front() == "en" && std::find(av.begin(), av.end(), "es") != av.end());
    // AI fills the rest into your data folder
    http.handler = [&](const HttpRequest&) {
        HttpResponse r;
        r.status = 200;
        r.body = R"({"choices":[{"message":{"content":"{\"Scan now\": \"Escanear ahora\"}"}}]})";
        return r;
    };
    AiClient ai(http, "Groq", "k", "m");
    CHECK_EQ(tr.translateWithAi(ai), 1);
    CHECK_EQ(tr.t("Scan now"), std::string("Escanear ahora"));
    CHECK(fs::exists(e.config.paths().dataDir / "data" / "i18n" / "es.json"));
}

TEST(my_builds) {
    TempEnv env;
    FakeHttp http;
    Engine e(env.paths(), http);
    MyBuilds mb(e.config.paths());
    CHECK_EQ(mb.all().size(), size_t(0));
    Json b = Json::parse(R"({"game":"mw4","weapon":"  M4  ","class":"Assault rifle","mode":"multiplayer","playstyle":"rusher",
      "attachments":[["Muzzle","Comp"],["Optic",""],["bad"]],"perks":"Ghost, Quick Fix ,","notes":"mine"})");
    std::string id = mb.save(b);
    CHECK(startsWith(id, "my-"));
    Json all = mb.all();
    CHECK_EQ(all.size(), size_t(1));
    const Json& one = all.elements()[0];
    CHECK_EQ(jstr(one, "weapon"), std::string("M4"));
    CHECK_EQ(one.get("attachments").size(), size_t(1));  // empty and malformed slots dropped
    CHECK_EQ(one.get("perks").size(), size_t(2));
    std::string created = jstr(one, "created");
    // edit keeps the id and the created date
    Json edit = one;
    edit["notes"] = "edited";
    edit["created"] = "";
    CHECK_EQ(mb.save(edit), id);
    CHECK_EQ(mb.all().size(), size_t(1));
    CHECK_EQ(jstr(mb.all().elements()[0], "notes"), std::string("edited"));
    CHECK_EQ(jstr(mb.all().elements()[0], "created"), created);
    // bad builds are refused
    bool threw = false;
    try { mb.save(Json::parse(R"({"game":"nope","weapon":"X"})")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { mb.save(Json::parse(R"({"game":"bo7","weapon":" "})")); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // shows up in recommendations as "yours"
    Json cands = buildCandidates(e.config, "mw4", Json::array());
    bool found = false;
    for (auto& c : cands.elements()) if (jstr(c, "origin") == "yours" && jstr(c, "weapon") == "M4") found = true;
    CHECK(found);
    // export → import into a fresh folder
    std::string dump = mb.exportJson();
    TempEnv env2;
    Engine e2(env2.paths(), http);
    MyBuilds mb2(e2.config.paths());
    CHECK_EQ(mb2.importJson(dump), 1);
    CHECK_EQ(jstr(mb2.all().elements()[0], "weapon"), std::string("M4"));
    CHECK(mb.remove(id));
    CHECK(!mb.remove(id));
    CHECK_EQ(mb.all().size(), size_t(0));
}

int main(int argc, char** argv) {
    std::string only = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (auto& [name, fn] : registry()) {
        if (!only.empty() && name.find(only) == std::string::npos) continue;
        int before = g_failed;
        try { fn(); } catch (const std::exception& e) { ++g_failed; std::cerr << "  EXCEPTION: " << e.what() << "\n"; }
        std::cout << (g_failed == before ? "ok    " : "FAIL  ") << name << "\n";
        ++ran;
    }
    std::cout << ran << " tests, " << g_checks << " checks, " << g_failed << " failures\n";
    return g_failed ? 1 : 0;
}
