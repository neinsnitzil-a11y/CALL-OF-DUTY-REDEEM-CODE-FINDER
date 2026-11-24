// HTML → Document (one block per paragraph / list item / table row, tagged with its heading),
// RSS/Atom feeds → items, and robots.txt rules.
#pragma once
#include <string>
#include <vector>

#include "extraction.hpp"

namespace hunter {

Document htmlToDocument(const std::string& html, const std::string& url = "");
std::string htmlToText(const std::string& html);
bool looksJsRendered(const std::string& html, const std::string& visibleText);
std::vector<std::string> embeddedStrings(const std::string& jsonText, size_t maxItems = 400);

struct Link {
    std::string url, text;  // url resolved against the page; text = the link's visible words
};
std::vector<Link> extractLinks(const std::string& html, const std::string& baseUrl);
struct ImageTag {
    std::string src, alt, title;  // src prefers data-src / data-lazy-src (lazy-loaded images)
};
std::vector<ImageTag> extractImages(const std::string& html);

struct FeedItem {
    std::string title, link, summary, published;
};
std::vector<FeedItem> parseFeedItems(const std::string& xml);
std::vector<Document> parseFeed(const std::string& xml, const std::string& fallbackUrl);

// robots.txt: the groups for our user agent (or *), longest matching Allow/Disallow wins.
class Robots {
public:
    Robots() = default;
    explicit Robots(const std::string& text, const std::string& userAgent);
    bool allows(const std::string& url) const;

private:
    struct Rule {
        std::string path;
        bool allow;
    };
    std::vector<Rule> rules_;
};

std::string googleNewsUrl(const std::string& query, int days = 2, const std::string& lang = "en",
                          const std::string& country = "US");
std::string realResultUrl(const std::string& link);  // unwrap google.com/url?url=…

}  // namespace hunter
