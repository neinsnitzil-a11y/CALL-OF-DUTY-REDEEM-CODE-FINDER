#include "ai.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <regex>
#include <set>
#include <thread>

#include "util.hpp"

namespace hunter {

const std::vector<AiPreset>& aiPresets() {
    static const std::vector<AiPreset> p = {
        {"OpenAI", "openai", "https://api.openai.com/v1", true},
        {"Anthropic (Claude)", "anthropic", "https://api.anthropic.com/v1", true},
        {"Google Gemini", "gemini", "https://generativelanguage.googleapis.com/v1beta", true},
        {"OpenRouter", "openai", "https://openrouter.ai/api/v1", true},
        {"Groq", "openai", "https://api.groq.com/openai/v1", true},
        {"Mistral", "openai", "https://api.mistral.ai/v1", true},
        {"DeepSeek", "openai", "https://api.deepseek.com/v1", true},
        {"xAI (Grok)", "openai", "https://api.x.ai/v1", true},
        {"Together", "openai", "https://api.together.xyz/v1", true},
        {"Perplexity", "openai", "https://api.perplexity.ai", true},
        {"Ollama (local)", "openai", "http://localhost:11434/v1", false},
        {"LM Studio (local)", "openai", "http://localhost:1234/v1", false},
        {"Custom (OpenAI-compatible)", "openai", "", true},
    };
    return p;
}

namespace {
const char* kAnthropicVersion = "2023-06-01";

bool isLocal(const std::string& url) {
    std::string h = parseUrl(url).host;
    size_t colon = h.rfind(':');
    if (colon != std::string::npos && h.find(']') == std::string::npos) h = h.substr(0, colon);
    return h == "localhost" || h == "127.0.0.1" || h == "[::1]" || h == "::1";
}

std::string errorText(const HttpResponse& r) {
    try {
        Json d = Json::parse(r.body);
        const Json& e = d.contains("error") ? d.get("error") : d;
        std::string m = e.isObject() ? jstr(e, "message", e.dump()) : e.toString(e.dump());
        return truncateUtf8(m, 200);
    } catch (...) {
        return truncateUtf8(r.body, 200);
    }
}

Json obj(std::initializer_list<std::pair<const char*, Json>> kv) {
    Json o = Json::object();
    for (auto& [k, v] : kv) o[k] = v;
    return o;
}
}  // namespace

AiClient::AiClient(HttpClient& http, std::string provider, std::string key, std::string model, std::string base,
                   int timeoutSeconds, int tries)
    : http_(http), timeout_(timeoutSeconds), tries_(tries) {
    provider_ = provider.empty() ? "OpenAI" : provider;
    const AiPreset* preset = nullptr;
    for (const auto& p : aiPresets()) if (p.name == provider_) preset = &p;
    if (!preset) {  // "openai" / "anthropic" / "gemini" → that wire's preset
        std::string w = lower(provider_);
        const char* byWire = w == "anthropic" ? "Anthropic (Claude)" : w == "gemini" ? "Google Gemini" : "Custom (OpenAI-compatible)";
        for (const auto& p : aiPresets()) if (p.name == byWire) preset = &p;
    }
    wire_ = preset->wire;
    needsKey_ = preset->needsKey;
    base_ = trim(base).empty() ? preset->base : trim(base);
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
    key_ = trim(key);
    model_ = trim(model);
    sleeper = [](double s) { std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(s * 1000))); };
}

std::string AiClient::problem() const {
    if (base_.empty()) return "Set the server address (AI_BASE_URL) for a custom provider.";
    if (!startsWith(base_, "https://") && !isLocal(base_)) return "The AI address must be https:// (plain http is only allowed for localhost).";
    if (needsKey_ && key_.empty() && !isLocal(base_)) return "Add your AI API key on the AI helper page.";
    if (model_.empty()) return "Pick a model (click List models).";
    return "";
}

std::map<std::string, std::string> AiClient::headers() const {
    std::map<std::string, std::string> h{{"User-Agent", "COD-Redemption-Hunter"}};
    if (wire_ == "anthropic") { h["x-api-key"] = key_; h["anthropic-version"] = kAnthropicVersion; }
    else if (wire_ == "gemini") h["x-goog-api-key"] = key_;
    else if (!key_.empty()) h["Authorization"] = "Bearer " + key_;
    return h;
}

Json AiClient::request(const std::string& method, const std::string& url, const Json* payload, int tries) {
    std::string last;
    tries = tries ? tries : tries_;
    for (int attempt = 0; attempt < tries; ++attempt) {
        HttpRequest req;
        req.method = method;
        req.url = url;
        req.headers = headers();
        req.timeoutSeconds = timeout_;
        if (payload) { req.body = payload->dump(); req.contentType = "application/json"; }
        HttpResponse r = http_.send(req);
        if (r.status == 0) {
            last = "couldn't reach " + parseUrl(url).host + (r.error.empty() ? "" : " (" + r.error + ")");
            if (attempt + 1 < tries && sleeper) sleeper(1 + 2 * attempt);
            continue;
        }
        if (r.status == 429 || r.status >= 500) {
            double wait = 2.0 * (attempt + 1);
            std::string ra = r.header("retry-after");
            if (!ra.empty()) wait = std::min(30.0, Json(ra).toDouble(wait));
            last = "HTTP " + std::to_string(r.status) + ": " +
                   (r.status == 429 ? "rate limited / out of free quota" : "the provider's server had an error") + " — " + errorText(r);
            if (attempt + 1 < tries && sleeper) sleeper(wait);
            continue;
        }
        if (r.status == 401 || r.status == 403)
            throw AiError("The AI provider rejected the API key (HTTP " + std::to_string(r.status) + ").");
        if (r.status == 404) throw AiError("Not found (HTTP 404) — check the model name / server address. " + errorText(r));
        if (r.status >= 400) throw AiError("AI request failed (HTTP " + std::to_string(r.status) + "): " + errorText(r));
        try {
            return Json::parse(r.body);
        } catch (...) {
            throw AiError("The AI provider sent back something that isn't JSON.");
        }
    }
    throw AiError("AI request failed: " + last);
}

std::vector<std::string> AiClient::listModels() {
    std::vector<std::string> out;
    if (wire_ == "anthropic") {
        Json d = request("GET", base_ + "/models?limit=100", nullptr);
        for (const auto& m : d.get("data").elements()) out.push_back(jstr(m, "id"));
    } else if (wire_ == "gemini") {
        Json d = request("GET", base_ + "/models?pageSize=200", nullptr);
        for (const auto& m : d.get("models").elements()) {
            const Json& methods = m.get("supportedGenerationMethods");
            bool ok = !methods.isArray();
            for (const auto& x : methods.elements()) if (x.toString() == "generateContent") ok = true;
            std::string name = jstr(m, "name");
            if (ok) out.push_back(name.substr(name.find('/') == std::string::npos ? 0 : name.find('/') + 1));
        }
    } else {
        Json d = request("GET", base_ + "/models", nullptr);
        for (const auto& m : d.get("data").elements()) if (!jstr(m, "id").empty()) out.push_back(jstr(m, "id"));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string AiClient::nativeSearch() const {
    std::string m = lower(model_);
    if (wire_ == "anthropic") return "Claude web search";
    if (wire_ == "gemini") return "Google Search";
    if (base_.find("openrouter.ai") != std::string::npos) return "OpenRouter web search";
    if (base_.find("perplexity.ai") != std::string::npos) return "Perplexity";
    if (base_.find("api.openai.com") != std::string::npos && m.find("search") != std::string::npos) return "OpenAI web search";
    return "";
}

std::pair<std::string, Json> AiClient::build(const std::string& prompt, const std::string& system,
                                             const std::vector<AiImage>& images, int maxTokens, const AiHistory& history,
                                             bool stream, bool web) const {
    if (wire_ == "anthropic") {
        Json content = Json::array();
        for (const auto& img : images)
            content.push(obj({{"type", "image"},
                              {"source", obj({{"type", "base64"}, {"media_type", img.mime}, {"data", base64Encode(img.bytes)}})}}));
        content.push(obj({{"type", "text"}, {"text", prompt}}));
        Json msgs = Json::array();
        for (const auto& [role, text] : history) msgs.push(obj({{"role", role}, {"content", text}}));
        msgs.push(obj({{"role", "user"}, {"content", content}}));
        Json body = obj({{"model", model_}, {"max_tokens", maxTokens}, {"messages", msgs}});
        if (!system.empty()) body["system"] = system;
        if (stream) body["stream"] = true;
        if (web) {
            Json tools = Json::array();
            tools.push(obj({{"type", "web_search_20250305"}, {"name", "web_search"}, {"max_uses", 5}}));
            body["tools"] = tools;
        }
        return {base_ + "/messages", body};
    }
    if (wire_ == "gemini") {
        Json parts = Json::array();
        for (const auto& img : images)
            parts.push(obj({{"inline_data", obj({{"mime_type", img.mime}, {"data", base64Encode(img.bytes)}})}}));
        parts.push(obj({{"text", prompt}}));
        Json contents = Json::array();
        for (const auto& [role, text] : history) {
            Json p = Json::array();
            p.push(obj({{"text", text}}));
            contents.push(obj({{"role", role == "assistant" ? "model" : "user"}, {"parts", p}}));
        }
        contents.push(obj({{"role", "user"}, {"parts", parts}}));
        // newer Gemini models "think" first and that counts toward the limit, so leave room
        Json body = obj({{"contents", contents}, {"generationConfig", obj({{"maxOutputTokens", std::max(maxTokens * 4, 2048)}})}});
        if (!system.empty()) {
            Json p = Json::array();
            p.push(obj({{"text", system}}));
            body["systemInstruction"] = obj({{"parts", p}});
        }
        if (web) {
            Json tools = Json::array();
            tools.push(obj({{"google_search", Json::object()}}));
            body["tools"] = tools;
        }
        return {base_ + "/models/" + model_ + ":" + (stream ? "streamGenerateContent?alt=sse" : "generateContent"), body};
    }
    Json msgs = Json::array();
    if (!system.empty()) msgs.push(obj({{"role", "system"}, {"content", system}}));
    for (const auto& [role, text] : history) msgs.push(obj({{"role", role}, {"content", text}}));
    if (images.empty()) {
        msgs.push(obj({{"role", "user"}, {"content", prompt}}));
    } else {
        Json content = Json::array();
        content.push(obj({{"type", "text"}, {"text", prompt}}));
        for (const auto& img : images)
            content.push(obj({{"type", "image_url"}, {"image_url", obj({{"url", "data:" + img.mime + ";base64," + base64Encode(img.bytes)}})}}));
        msgs.push(obj({{"role", "user"}, {"content", content}}));
    }
    Json body = obj({{"model", model_}, {"messages", msgs}, {"max_tokens", maxTokens}});
    if (stream) body["stream"] = true;
    if (web && base_.find("openrouter.ai") != std::string::npos) {
        Json tools = Json::array();
        tools.push(obj({{"type", "openrouter:web_search"}}));
        body["tools"] = tools;
    } else if (web && base_.find("api.openai.com") != std::string::npos && lower(model_).find("search") != std::string::npos) {
        body["web_search_options"] = Json::object();
        body.erase("max_tokens");
    }
    return {base_ + "/chat/completions", body};
}

std::vector<AiSource> AiClient::sourcesOf(const Json& chunk) {
    std::vector<AiSource> out;
    if (!chunk.isObject()) return out;
    auto results = [&](const Json& block) {
        if (jstr(block, "type") == "web_search_tool_result" && block.get("content").isArray())
            for (const auto& r : block.get("content").elements())
                if (r.isObject()) out.emplace_back(jstr(r, "url"), jstr(r, "title"));
    };
    results(chunk.get("content_block"));
    for (const auto& b : chunk.get("content").elements()) if (b.isObject()) results(b);
    for (const auto& c : chunk.get("candidates").elements())
        for (const auto& g : c.get("groundingMetadata").get("groundingChunks").elements())
            out.emplace_back(jstr(g.get("web"), "uri"), jstr(g.get("web"), "title"));
    for (const auto& ch : chunk.get("choices").elements()) {
        const Json& part = ch.get("delta").isObject() ? ch.get("delta") : ch.get("message");
        for (const auto& a : part.get("annotations").elements()) {
            if (a.get("url_citation").isObject()) out.emplace_back(jstr(a.get("url_citation"), "url"), jstr(a.get("url_citation"), "title"));
            else if (!jstr(a, "url").empty()) out.emplace_back(jstr(a, "url"), jstr(a, "title"));
        }
    }
    for (const auto& u : chunk.get("citations").elements()) if (u.isString()) out.emplace_back(u.toString(), "");
    for (const auto& r : chunk.get("search_results").elements())
        if (!jstr(r, "url").empty()) out.emplace_back(jstr(r, "url"), jstr(r, "title"));
    out.erase(std::remove_if(out.begin(), out.end(), [](const AiSource& s) { return !startsWith(s.first, "http"); }), out.end());
    return out;
}

std::string AiClient::textOf(const Json& data) const {
    std::string out;
    if (wire_ == "anthropic") {
        if (jstr(data, "type") == "content_block_delta") return jstr(data.get("delta"), "text");
        for (const auto& b : data.get("content").elements()) if (jstr(b, "type") == "text") out += jstr(b, "text");
        return out;
    }
    if (wire_ == "gemini") {
        const auto& cands = data.get("candidates").elements();
        if (cands.empty()) return "";
        for (const auto& p : cands[0].get("content").get("parts").elements())
            if (!jbool(p, "thought")) out += jstr(p, "text");
        return out;
    }
    const auto& choices = data.get("choices").elements();
    if (choices.empty()) return "";
    const Json& part = choices[0].get("delta").isObject() ? choices[0].get("delta") : choices[0].get("message");
    const Json& content = part.get("content");
    if (content.isArray()) {
        for (const auto& x : content.elements()) out += jstr(x, "text");
        return out;
    }
    return content.toString();
}

std::string AiClient::chat(const std::string& prompt, const std::string& system, const std::vector<AiImage>& images,
                           int maxTokens, const AiHistory& history) {
    if (auto p = problem(); !p.empty()) throw AiError(p);
    auto [url, body] = build(prompt, system, images, maxTokens, history, false, false);
    Json data = request("POST", url, &body);
    std::string text = trim(textOf(data));
    if (text.empty()) {
        std::string reason;
        if (wire_ == "gemini") {
            const auto& c = data.get("candidates").elements();
            reason = c.empty() ? jstr(data.get("promptFeedback"), "blockReason") : jstr(c[0], "finishReason");
        } else if (wire_ == "anthropic") {
            reason = jstr(data, "stop_reason");
        } else if (!data.get("choices").elements().empty()) {
            reason = jstr(data.get("choices").elements()[0], "finish_reason");
        }
        static const std::set<std::string> blocked = {"SAFETY", "PROHIBITED_CONTENT", "BLOCKLIST", "content_filter", "refusal"};
        if (blocked.count(reason)) throw AiError("The provider's filter blocked the answer (" + reason + ").");
        if (wire_ == "gemini" && data.get("candidates").elements().empty())
            throw AiError("The AI returned no answer (it may have been blocked by the provider's filters).");
    }
    return text;
}

std::string AiClient::chatStream(const std::string& prompt, const std::function<void(const std::string&)>& onText,
                                 const std::string& system, const std::vector<AiImage>& images, int maxTokens,
                                 const AiHistory& history, bool web,
                                 const std::function<void(const std::string&, const std::string&)>& onSource,
                                 const std::function<void(const std::string&)>& onStatus) {
    if (auto p = problem(); !p.empty()) throw AiError(p);
    auto [url, body] = build(prompt, system, images, maxTokens, history, true, web);
    std::string got, pending, streamError;
    std::set<std::string> seen;
    bool done = false;
    auto handleLine = [&](std::string line) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!startsWith(line, "data:")) return;
        std::string data = trim(line.substr(5));
        if (data == "[DONE]") { done = true; return; }
        Json chunk;
        try { chunk = Json::parse(data); } catch (...) { return; }
        if (jstr(chunk, "type") == "error" || (!chunk.get("error").isNull() && chunk.get("choices").isNull())) {
            const Json& e = chunk.get("error");
            streamError = e.isObject() ? jstr(e, "message", e.dump()) : e.toString(chunk.dump());
            done = true;
            return;
        }
        if (onSource)
            for (const auto& [u, t] : sourcesOf(chunk))
                if (seen.insert(u).second) onSource(u, t);
        if (onStatus && jstr(chunk.get("content_block"), "type") == "server_tool_use") onStatus("🔎 searching the web…");
        std::string piece = textOf(chunk);
        if (!piece.empty()) {
            got += piece;
            onText(piece);
        }
    };
    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.headers = headers();
    req.headers["Accept"] = "text/event-stream";
    req.body = body.dump();
    req.contentType = "application/json";
    req.timeoutSeconds = timeout_;
    HttpResponse r = http_.stream(req, [&](const std::string& data) {
        if (done || (cancel && cancel->load())) return false;
        pending += data;
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            handleLine(line);
            if (done) break;
        }
        return !done;
    });
    if (cancel && cancel->load()) throw AiError("stopped");
    if (!pending.empty() && !done) handleLine(pending);
    if (r.status == 401 || r.status == 403) throw AiError("The AI provider rejected the API key (HTTP " + std::to_string(r.status) + ").");
    if (r.status >= 400) throw AiError("AI request failed (HTTP " + std::to_string(r.status) + "): " + errorText(r));
    if (!streamError.empty()) throw AiError("AI error: " + streamError);
    if (got.empty()) {
        std::string text = chat(prompt, system, images, maxTokens, history);  // no streaming here: ask normally
        onText(text);
        return text;
    }
    if (r.status == 0) throw AiError("The connection dropped mid-answer (" + r.error + ").");
    return trim(got);
}

Json AiClient::chatJson(const std::string& prompt, const std::string& system, const std::vector<AiImage>& images, int maxTokens) {
    return parseAiJson(chat(prompt + "\n\nReply with JSON only, no other text.", system, images, maxTokens));
}

std::pair<bool, std::string> AiClient::test() {
    AiClient quick(http_, provider_, key_, model_, base_, 25, 1);
    quick.sleeper = sleeper;
    try {
        std::string answer = quick.chat("Reply with the single word: ready", "", {}, 256);
        return {true, "✔ Connected to " + provider_ + " · " + model_ + (answer.empty() ? "" : ": “" + truncateUtf8(answer, 30) + "”")};
    } catch (const std::exception& e) {
        return {false, std::string("✖ ") + e.what()};
    }
}

Json parseAiJson(const std::string& textIn) {
    std::string text = trim(textIn);
    size_t fence = text.find("```");
    if (fence != std::string::npos) {
        size_t start = text.find('\n', fence);
        size_t end = text.find("```", fence + 3);
        if (start != std::string::npos && end != std::string::npos && end > start) text = trim(text.substr(start + 1, end - start - 1));
    }
    for (auto [open, close] : {std::pair<char, char>{'{', '}'}, std::pair<char, char>{'[', ']'}}) {
        size_t i = text.find(open), j = text.rfind(close);
        if (i != std::string::npos && j != std::string::npos && j > i) {
            try { return Json::parse(text.substr(i, j - i + 1)); } catch (...) {}
        }
    }
    throw AiError("The AI's answer wasn't valid JSON.");
}

std::string suggestModel(const std::vector<std::string>& models) {
    static const char* prefs[] = {"flash", "mini", "haiku", "sonnet", "small", "instant", "8b", "llama", "gpt", "gemini", "claude"};
    static const char* skip[] = {"embed", "tts", "whisper", "audio", "image-gen", "dall", "moderation", "imagen", "veo", "aqa",
                                 "transcribe", "realtime", "search"};
    std::vector<std::string> usable;
    for (const auto& m : models) {
        std::string l = lower(m);
        if (std::none_of(std::begin(skip), std::end(skip), [&](const char* s) { return l.find(s) != std::string::npos; }))
            usable.push_back(m);
    }
    for (const char* p : prefs)
        for (const auto& m : usable) if (lower(m).find(p) != std::string::npos) return m;
    if (!usable.empty()) return usable.front();
    return models.empty() ? "" : models.front();
}

}  // namespace hunter
