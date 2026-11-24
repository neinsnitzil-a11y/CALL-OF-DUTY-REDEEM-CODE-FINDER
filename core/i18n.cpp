#include "i18n.hpp"

#include <algorithm>
#include <set>

#include "util.hpp"

namespace hunter {

const std::vector<std::pair<std::string, std::string>>& languageNames() {
    static const std::vector<std::pair<std::string, std::string>> l = {
        {"en", "English"}, {"es", "Español"}, {"pt", "Português (Brasil)"}, {"id", "Bahasa Indonesia"}, {"hi", "हिन्दी"},
        {"fr", "Français"}, {"de", "Deutsch"}, {"ar", "العربية"}, {"tr", "Türkçe"}};
    return l;
}

std::string englishLanguageName(const std::string& code) {
    static const std::map<std::string, std::string> n = {{"en", "English"}, {"es", "Spanish"}, {"pt", "Brazilian Portuguese"},
                                                         {"id", "Indonesian"}, {"hi", "Hindi"}, {"fr", "French"},
                                                         {"de", "German"}, {"ar", "Arabic"}, {"tr", "Turkish"}};
    auto it = n.find(code);
    return it == n.end() ? code : it->second;
}

namespace {
std::map<std::string, std::string> readTable(const fs::path& p) {
    std::map<std::string, std::string> out;
    try {
        Json d = Json::parse(readFile(p));
        for (const auto& [k, v] : d.items())
            if (!startsWith(k, "_") && v.isString()) out[k] = v.toString();
    } catch (...) {}
    return out;
}
}  // namespace

void Translator::load(const std::string& code) {
    code_ = code.empty() ? "en" : code;
    table_.clear();
    if (code_ == "en") return;
    table_ = readTable(paths_.appDir / "i18n" / (code_ + ".json"));
    for (auto& [k, v] : readTable(paths_.dataDir / "data" / "i18n" / (code_ + ".json"))) table_[k] = v;
}

std::string Translator::t(const std::string& english) const {
    if (table_.empty() || english.empty()) return english;
    auto it = table_.find(english);
    return it == table_.end() ? english : it->second;
}

std::vector<std::string> Translator::available() const {
    std::set<std::string> codes;
    for (auto& [c, n] : languageNames()) codes.insert(c);
    for (const fs::path& dir : {paths_.appDir / "i18n", paths_.dataDir / "data" / "i18n"}) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) continue;
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".json" && startsWith(e.path().stem().string(), "strings") == false) codes.insert(e.path().stem().string());
    }
    std::vector<std::string> out(codes.begin(), codes.end());
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return (a != "en") < (b != "en"); });
    return out;
}

std::vector<std::string> Translator::englishStrings() const {
    std::vector<std::string> out;
    try {
        const Json d = Json::parse(readFile(paths_.appDir / "i18n" / "strings.json"));
        for (auto& s : d.get("strings").elements()) out.push_back(s.toString());
    } catch (...) {}
    return out;
}

std::vector<std::string> Translator::missing() const {
    std::vector<std::string> out;
    for (auto& s : englishStrings()) if (!table_.count(s)) out.push_back(s);
    return out;
}

int Translator::translateWithAi(AiClient& ai, const std::function<void(const std::string&)>& onLine, size_t batch) {
    auto todo = missing();
    std::string lang = englishLanguageName(code_);
    fs::path path = paths_.dataDir / "data" / "i18n" / (code_ + ".json");
    Json have = Json::object();
    try { have = Json::parse(readFile(path)); } catch (...) {}
    if (!have.isObject()) have = Json::object();
    int added = 0;
    for (size_t i = 0; i < todo.size(); i += batch) {
        Json chunk = Json::array();
        std::set<std::string> wanted;
        for (size_t k = i; k < std::min(todo.size(), i + batch); ++k) { chunk.push(todo[k]); wanted.insert(todo[k]); }
        if (onLine) onLine("Translating " + std::to_string(std::min(todo.size(), i + batch)) + " / " + std::to_string(todo.size()) + " …");
        std::string prompt = "Translate these app interface strings from English to " + lang +
                             ". Keep emojis, symbols, numbers, '%1'-style placeholders and names like Call of Duty, Discord, "
                             "GitHub, Warzone, CODM unchanged. Short and natural, like a game app. Return a JSON object mapping "
                             "each English string to its translation.\n\n" + chunk.dump();
        Json data = ai.chatJson(prompt, "", {}, 6000);
        for (const auto& [k, v] : data.items())
            if (wanted.count(k) && (v.isString() || v.isNumber())) { have[k] = v.toString(); ++added; }
        writeFileAtomic(path, have.dump(1));
    }
    load(code_);
    return added;
}

}  // namespace hunter
