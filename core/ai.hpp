// One client for any AI API. Three wire formats cover practically every provider:
//   openai     /chat/completions — OpenAI, OpenRouter, Groq, Mistral, DeepSeek, xAI, Together, Perplexity,
//              and local servers (Ollama, LM Studio, llama.cpp, vLLM…)
//   anthropic  Anthropic Messages API (Claude)
//   gemini     Google Gemini generateContent
// Settings live in your private .env: AI_PROVIDER, AI_API_KEY, AI_MODEL, AI_BASE_URL.
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "http.hpp"
#include "json.hpp"

namespace hunter {

class AiError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AiPreset {
    std::string name, wire, base;
    bool needsKey;
};
const std::vector<AiPreset>& aiPresets();

struct AiImage {
    std::string mime;  // image/png, image/jpeg, …
    std::string bytes;
};
using AiHistory = std::vector<std::pair<std::string, std::string>>;  // (role user|assistant, text)
using AiSource = std::pair<std::string, std::string>;                 // (url, title)

class AiClient {
public:
    AiClient(HttpClient& http, std::string provider, std::string key, std::string model, std::string base = "",
             int timeoutSeconds = 120, int tries = 3);

    std::string problem() const;  // why it can't be used yet, or ""
    bool ready() const { return problem().empty(); }
    const std::string& provider() const { return provider_; }
    const std::string& model() const { return model_; }
    const std::string& wire() const { return wire_; }
    std::string nativeSearch() const;  // name of the provider's own live web search, or ""

    std::vector<std::string> listModels();
    std::string chat(const std::string& prompt, const std::string& system = "", const std::vector<AiImage>& images = {},
                     int maxTokens = 1500, const AiHistory& history = {});
    // Live typing: onText(piece) as the answer arrives; onSource(url, title) for cited web pages;
    // onStatus("searching the web…"). Falls back to a normal request if streaming gives nothing.
    std::string chatStream(const std::string& prompt, const std::function<void(const std::string&)>& onText,
                           const std::string& system = "", const std::vector<AiImage>& images = {}, int maxTokens = 1500,
                           const AiHistory& history = {}, bool web = false,
                           const std::function<void(const std::string&, const std::string&)>& onSource = {},
                           const std::function<void(const std::string&)>& onStatus = {});
    Json chatJson(const std::string& prompt, const std::string& system = "", const std::vector<AiImage>& images = {},
                  int maxTokens = 2500);
    std::pair<bool, std::string> test();

    // exposed for tests
    std::pair<std::string, Json> build(const std::string& prompt, const std::string& system, const std::vector<AiImage>& images,
                                       int maxTokens, const AiHistory& history, bool stream, bool web) const;
    std::string textOf(const Json& data) const;
    static std::vector<AiSource> sourcesOf(const Json& chunk);
    std::map<std::string, std::string> headers() const;
    std::function<void(double)> sleeper;
    const std::atomic<bool>* cancel = nullptr;  // set → a streaming answer stops (throws AiError "stopped")

private:
    Json request(const std::string& method, const std::string& url, const Json* payload, int tries = 0);
    HttpClient& http_;
    std::string provider_, wire_, base_, key_, model_;
    bool needsKey_ = true;
    int timeout_, tries_;
};

Json parseAiJson(const std::string& text);  // tolerates ```json fences and chatter; throws AiError
std::string suggestModel(const std::vector<std::string>& models);

}  // namespace hunter
