#include "html.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>
#include <set>

#include "json.hpp"
#include "util.hpp"

namespace hunter {

namespace {
const std::set<std::string> kBlockTags = {"p", "li", "tr", "dd", "dt", "blockquote", "pre", "div", "section",
                                          "article", "figcaption", "caption", "summary"};
const std::set<std::string> kDropTags = {"script", "style", "noscript", "svg", "nav", "footer", "form",
                                         "iframe", "button", "select", "template"};
const std::set<std::string> kVoidTags = {"br", "img", "meta", "link", "input", "hr", "source", "wbr", "area", "base", "col"};

struct Tag {
    std::string name;
    bool closing = false, selfClosing = false;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string attr(const std::string& k) const {
        for (auto& [a, v] : attrs) if (a == k) return v;
        return "";
    }
};

// Parses "<name a=b c='d'>" starting after '<'. Returns false for things that aren't tags ("a < b").
bool parseTag(const std::string& h, size_t& i, Tag& t, bool xml = false) {
    size_t p = i;
    if (p < h.size() && h[p] == '/') { t.closing = true; ++p; }
    size_t start = p;
    while (p < h.size() && (std::isalnum(static_cast<unsigned char>(h[p])) || h[p] == '-' || h[p] == ':')) ++p;
    if (p == start) return false;
    t.name = lower(h.substr(start, p - start));
    while (p < h.size() && h[p] != '>') {
        while (p < h.size() && std::isspace(static_cast<unsigned char>(h[p]))) ++p;
        if (p < h.size() && h[p] == '/') { t.selfClosing = true; ++p; continue; }
        if (p >= h.size() || h[p] == '>') break;
        size_t ks = p;
        while (p < h.size() && !std::isspace(static_cast<unsigned char>(h[p])) && h[p] != '=' && h[p] != '>' && h[p] != '/') ++p;
        std::string key = lower(h.substr(ks, p - ks));
        std::string val;
        while (p < h.size() && std::isspace(static_cast<unsigned char>(h[p]))) ++p;
        if (p < h.size() && h[p] == '=') {
            ++p;
            while (p < h.size() && std::isspace(static_cast<unsigned char>(h[p]))) ++p;
            if (p < h.size() && (h[p] == '"' || h[p] == '\'')) {
                char q = h[p++];
                size_t vs = p;
                while (p < h.size() && h[p] != q) ++p;
                val = h.substr(vs, p - vs);
                if (p < h.size()) ++p;
            } else {
                size_t vs = p;
                while (p < h.size() && !std::isspace(static_cast<unsigned char>(h[p])) && h[p] != '>') ++p;
                val = h.substr(vs, p - vs);
            }
        }
        if (key.empty()) { ++p; continue; }
        t.attrs.emplace_back(key, decodeEntities(val));
    }
    i = p < h.size() ? p + 1 : p;
    if (!xml && kVoidTags.count(t.name)) t.selfClosing = true;
    return true;
}

// Index of the matching "</name" (case-insensitive) at or after i, or npos.
size_t findClose(const std::string& h, size_t i, const std::string& name) {
    std::string needle = "</" + name;
    while (true) {
        auto it = std::search(h.begin() + static_cast<long>(i), h.end(), needle.begin(), needle.end(),
                              [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
        if (it == h.end()) return std::string::npos;
        size_t pos = static_cast<size_t>(it - h.begin());
        char after = pos + needle.size() < h.size() ? h[pos + needle.size()] : '>';
        if (!std::isalnum(static_cast<unsigned char>(after))) return pos;
        i = pos + 1;
    }
}
}  // namespace

bool looksJsRendered(const std::string& html, const std::string& visibleText) {
    std::string low = lower(html);
    size_t scripts = 0;
    for (size_t p = 0; (p = low.find("<script", p)) != std::string::npos; ++p) ++scripts;
    return trim(visibleText).size() < 400 && scripts >= 5 && low.size() > 20000;
}

std::vector<std::string> embeddedStrings(const std::string& jsonText, size_t maxItems) {
    Json data;
    try { data = Json::parse(jsonText); } catch (...) { return {}; }
    std::vector<std::string> out;
    std::function<void(const Json&, const std::string&)> walk = [&](const Json& node, const std::string& key) {
        if (out.size() >= maxItems) return;
        if (node.isObject()) for (const auto& [k, v] : node.items()) walk(v, k);
        else if (node.isArray()) for (const auto& v : node.elements()) walk(v, key);
        else if (node.isString()) {
            std::string s = collapseSpaces(node.toString());
            if (s.size() >= 6 && s.size() <= 600 && (icontains(key, "code") || hasCodeContext(s))) out.push_back(s);
        }
    };
    walk(data, "");
    return out;
}

Document htmlToDocument(const std::string& html, const std::string& url) {
    std::string body;       // text with "\n" block breaks and "\x00H…" heading markers
    std::string title, h1, metaText;
    std::vector<std::string> embedded;
    std::string headingBuf;
    int headingDepth = 0;
    bool inTitle = false, sawBody = false, inHead = false;
    std::string titleBuf;

    auto emitText = [&](const std::string& raw) {
        std::string t = decodeEntities(raw);
        if (inTitle) { titleBuf += t; return; }
        if (inHead) return;
        if (headingDepth > 0) { headingBuf += " " + t; return; }
        body += " ";
        body += t;
    };

    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        size_t lt = html.find('<', i);
        if (lt == std::string::npos) { emitText(html.substr(i)); break; }
        if (lt > i) emitText(html.substr(i, lt - i));
        i = lt + 1;
        if (html.compare(i, 3, "!--") == 0) {  // comment
            size_t e = html.find("-->", i + 3);
            i = e == std::string::npos ? n : e + 3;
            continue;
        }
        if (i < n && (html[i] == '!' || html[i] == '?')) {  // doctype / processing instruction
            size_t e = html.find('>', i);
            i = e == std::string::npos ? n : e + 1;
            continue;
        }
        Tag t;
        size_t save = i;
        if (!parseTag(html, i, t)) { emitText("<"); i = save; continue; }
        const std::string& name = t.name;
        if (!t.closing) {
            if (name == "head") inHead = true;
            if (name == "body") { sawBody = true; inHead = false; }
            if (name == "title") { inTitle = true; continue; }
            if (name == "meta") {
                std::string nm = lower(t.attr("name")), prop = lower(t.attr("property"));
                if ((nm == "description" || (prop == "og:description" && metaText.empty())) && !t.attr("content").empty())
                    metaText = collapseSpaces(t.attr("content"));
                continue;
            }
            if (kDropTags.count(name) && !t.selfClosing) {
                size_t close = findClose(html, i, name);
                std::string inner = html.substr(i, close == std::string::npos ? std::string::npos : close - i);
                if (name == "script") {
                    std::string type = lower(t.attr("type")), id = t.attr("id");
                    if (type == "application/json" || type == "application/ld+json" || id == "__NEXT_DATA__" ||
                        id == "__NUXT_DATA__")
                        for (auto& s : embeddedStrings(inner)) embedded.push_back(s);
                }
                if (close == std::string::npos) { i = n; continue; }
                size_t gt = html.find('>', close);
                i = gt == std::string::npos ? n : gt + 1;
                continue;
            }
            if (name == "br") { if (headingDepth) headingBuf += " "; else body += "\n"; continue; }
            if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
                if (headingDepth++ == 0) headingBuf.clear();
                continue;
            }
            if (kBlockTags.count(name) && headingDepth == 0) body += "\n";
        } else {
            if (name == "title") {
                inTitle = false;
                if (title.empty()) title = collapseSpaces(titleBuf);
                continue;
            }
            if (name == "head") inHead = false;
            if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && headingDepth > 0) {
                if (--headingDepth == 0) {
                    std::string text = collapseSpaces(headingBuf);
                    if (name == "h1" && h1.empty()) h1 = text;
                    body += "\n";
                    body += '\0';
                    body += "H" + text + "\n";
                }
                continue;
            }
            if (kBlockTags.count(name) && headingDepth == 0) body += "\n";
        }
    }
    (void)sawBody;
    std::string docTitle = !h1.empty() ? h1 : title;
    Document doc{url, docTitle, {}};
    if (!metaText.empty()) doc.blocks.push_back({docTitle, metaText});
    std::string heading = docTitle;
    for (const auto& raw : split(body, '\n')) {
        if (!raw.empty() && raw[0] == '\0') {
            heading = trim(raw.substr(2));
            continue;
        }
        std::string line = collapseSpaces(raw);
        if (!line.empty()) doc.blocks.push_back({heading, line});
    }
    std::set<std::string> seen;
    for (auto& s : embedded)
        if (seen.insert(s).second) doc.blocks.push_back({docTitle + " (page data)", s});
    return doc;
}

std::string htmlToText(const std::string& html) {
    std::vector<std::string> lines;
    for (auto& b : htmlToDocument(html).blocks) lines.push_back(b.text);
    return join(lines, "\n");
}

std::vector<Link> extractLinks(const std::string& html, const std::string& baseUrl) {
    std::vector<Link> out;
    size_t i = 0;
    while ((i = html.find('<', i)) != std::string::npos) {
        size_t q = i + 1;
        Tag t;
        if (q >= html.size() || html[q] == '/' || html[q] == '!' || !parseTag(html, q, t) || t.name != "a" || t.attr("href").empty()) {
            ++i;
            continue;
        }
        size_t close = findClose(html, q, "a");
        std::string inner = html.substr(q, close == std::string::npos ? 0 : close - q);
        std::string text;  // strip tags from the link body
        bool inTag = false;
        for (char c : inner) {
            if (c == '<') { inTag = true; text += ' '; }
            else if (c == '>') inTag = false;
            else if (!inTag) text += c;
        }
        std::string url = resolveUrl(baseUrl, t.attr("href"));
        if (!url.empty()) out.push_back({url, collapseSpaces(decodeEntities(text))});
        i = close == std::string::npos ? q : close + 1;
    }
    return out;
}

std::vector<ImageTag> extractImages(const std::string& html) {
    std::vector<ImageTag> out;
    size_t i = 0;
    while ((i = html.find('<', i)) != std::string::npos) {
        size_t q = i + 1;
        Tag t;
        if (q < html.size() && html[q] != '/' && html[q] != '!' && parseTag(html, q, t) && t.name == "img") {
            std::string src = t.attr("data-src");
            if (src.empty()) src = t.attr("data-lazy-src");
            if (src.empty()) src = t.attr("src");
            out.push_back({src, t.attr("alt"), t.attr("title")});
            i = q;
        } else {
            ++i;
        }
    }
    return out;
}

// ── feeds ───────────────────────────────────────────────────────
namespace {
std::string localName(const std::string& tag) {
    size_t c = tag.rfind(':');
    return c == std::string::npos ? tag : tag.substr(c + 1);
}
// Text of an XML element body: CDATA kept raw (it's HTML), plain text entity-decoded.
std::string xmlText(const std::string& inner) {
    std::string out;
    size_t i = 0;
    while (i < inner.size()) {
        size_t cd = inner.find("<![CDATA[", i);
        if (cd == std::string::npos) { out += decodeEntities(inner.substr(i)); break; }
        out += decodeEntities(inner.substr(i, cd - i));
        size_t end = inner.find("]]>", cd + 9);
        out += inner.substr(cd + 9, end == std::string::npos ? std::string::npos : end - cd - 9);
        i = end == std::string::npos ? inner.size() : end + 3;
    }
    return out;
}
}  // namespace

std::vector<FeedItem> parseFeedItems(const std::string& xml) {
    std::vector<FeedItem> items;
    size_t i = 0;
    while (true) {
        // next <item> or <entry> (any namespace prefix)
        size_t best = std::string::npos;
        std::string openName;
        for (size_t p = xml.find('<', i); p != std::string::npos; p = xml.find('<', p + 1)) {
            size_t q = p + 1;
            Tag t;
            if (q < xml.size() && xml[q] != '/' && xml[q] != '!' && xml[q] != '?' && parseTag(xml, q, t, true)) {
                std::string ln = localName(t.name);
                if (ln == "item" || ln == "entry") { best = q; openName = t.name; break; }
            }
        }
        if (best == std::string::npos) break;
        size_t close = findClose(xml, best, openName);
        std::string body = xml.substr(best, close == std::string::npos ? std::string::npos : close - best);
        i = close == std::string::npos ? xml.size() : close + 1;

        FeedItem it;
        std::vector<std::string> parts;
        size_t k = 0;
        while ((k = body.find('<', k)) != std::string::npos) {
            size_t q = k + 1;
            Tag t;
            if (q >= body.size() || body[q] == '/' || body[q] == '!' || !parseTag(body, q, t, true)) { ++k; continue; }
            std::string ln = localName(t.name);
            std::string inner;
            size_t next = q;
            if (!t.selfClosing) {
                size_t c = findClose(body, q, t.name);
                inner = body.substr(q, c == std::string::npos ? std::string::npos : c - q);
                if (ln != "content" && ln != "description" && ln != "summary" && ln != "encoded")
                    next = q;  // descend into containers like <media:group>
                else
                    next = c == std::string::npos ? body.size() : c;
            }
            std::string text = trim(xmlText(inner));
            if (ln == "title" && it.title.empty()) it.title = text;
            else if (ln == "link" && it.link.empty()) it.link = !t.attr("href").empty() ? t.attr("href") : text;
            else if ((ln == "description" || ln == "summary" || ln == "content" || ln == "encoded") && !text.empty())
                parts.push_back(text);
            else if ((ln == "pubdate" || ln == "pubDate" || ln == "published" || ln == "updated") && it.published.empty())
                it.published = text;
            k = next;
        }
        it.summary = join(parts, " ");
        items.push_back(it);
    }
    return items;
}

std::vector<Document> parseFeed(const std::string& xml, const std::string& fallbackUrl) {
    std::vector<Document> docs;
    for (auto& it : parseFeedItems(xml))
        docs.push_back(htmlToDocument("<h1>" + escapeHtml(it.title) + "</h1>" + it.summary,
                                      it.link.empty() ? fallbackUrl : it.link));
    return docs;
}

// ── robots.txt ──────────────────────────────────────────────────
Robots::Robots(const std::string& text, const std::string& userAgent) {
    std::string agent = lower(userAgent.substr(0, userAgent.find('/')));
    std::vector<Rule> mine, star;
    bool groupMine = false, groupStar = false, lastWasAgent = false;
    bool foundMine = false;
    for (auto line : splitLines(text)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, colon))), val = trim(line.substr(colon + 1));
        if (key == "user-agent") {
            if (!lastWasAgent) { groupMine = groupStar = false; }
            std::string v = lower(val);
            if (v == "*") groupStar = true;
            else if (!agent.empty() && agent.find(v) != std::string::npos) { groupMine = true; foundMine = true; }
            lastWasAgent = true;
            continue;
        }
        lastWasAgent = false;
        if (key != "allow" && key != "disallow") continue;
        if (key == "disallow" && val.empty()) continue;  // "Disallow:" = allow everything
        Rule r{val, key == "allow"};
        if (groupMine) mine.push_back(r);
        if (groupStar) star.push_back(r);
    }
    rules_ = foundMine ? mine : star;
}

bool Robots::allows(const std::string& url) const {
    Url u = parseUrl(url);
    std::string path = u.path + (u.query.empty() ? "" : "?" + u.query);
    const Rule* best = nullptr;
    for (const auto& r : rules_) {
        // support '*' wildcards and a trailing '$'
        std::string pat = r.path;
        bool anchored = !pat.empty() && pat.back() == '$';
        if (anchored) pat.pop_back();
        std::string rx = "^";
        for (char c : pat) {
            if (c == '*') rx += ".*";
            else if (std::string(R"(\^$.|?+()[]{})").find(c) != std::string::npos) { rx += '\\'; rx += c; }
            else rx += c;
        }
        if (anchored) rx += "$";
        if (!std::regex_search(path, std::regex(rx))) continue;
        if (!best || r.path.size() > best->path.size() || (r.path.size() == best->path.size() && r.allow)) best = &r;
    }
    return !best || best->allow;
}

std::string googleNewsUrl(const std::string& query, int days, const std::string& lang, const std::string& country) {
    std::string q = days ? query + " when:" + std::to_string(days) + "d" : query;
    return "https://news.google.com/rss/search?q=" + urlEncode(q) + "&hl=" + lang + "-" + country + "&gl=" + country +
           "&ceid=" + country + ":" + lang;
}

std::string realResultUrl(const std::string& link) {
    Url u = parseUrl(link);
    if (endsWith(u.host, "google.com") && u.path == "/url") {
        std::string t = queryParam(u.query, "url");
        if (t.empty()) t = queryParam(u.query, "q");
        if (!t.empty()) return t;
    }
    return link;
}

}  // namespace hunter
