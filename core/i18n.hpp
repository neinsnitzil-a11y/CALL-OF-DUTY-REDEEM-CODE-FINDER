// Languages. English is built in; each other language is data/i18n/<code>.json mapping English text to
// its translation (yours in <data folder>/data/i18n/ override the shipped ones). Untranslated text stays
// English. Any language can be added or completed with your AI key (translateWithAi).
#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "ai.hpp"
#include "config.hpp"

namespace hunter {

const std::vector<std::pair<std::string, std::string>>& languageNames();  // code → native name
std::string englishLanguageName(const std::string& code);

class Translator {
public:
    explicit Translator(const Paths& paths) : paths_(paths) {}
    void load(const std::string& code);
    const std::string& code() const { return code_; }
    std::string t(const std::string& english) const;  // the translation, or the English text
    bool has(const std::string& english) const { return table_.count(english) > 0; }
    std::vector<std::string> available() const;
    std::vector<std::string> englishStrings() const;  // every UI string (data/i18n/strings.json)
    std::vector<std::string> missing() const;
    // Fills untranslated strings with the AI, in batches; returns how many were added.
    int translateWithAi(AiClient& ai, const std::function<void(const std::string&)>& onLine = {}, size_t batch = 60);
    bool rightToLeft() const { return code_ == "ar"; }

private:
    const Paths& paths_;
    std::string code_ = "en";
    std::map<std::string, std::string> table_;
};

}  // namespace hunter
