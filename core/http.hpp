// The core never talks to the network itself: the app plugs in an HttpClient (Qt Network on the
// desktop, a fake in tests). Calls are blocking and made from the scanner thread.
#pragma once
#include <functional>
#include <map>
#include <string>

namespace hunter {

struct HttpResponse {
    int status = 0;                       // 0 = network error (see error)
    std::string body, finalUrl, error;
    std::map<std::string, std::string> headers;  // lower-case names
    std::string header(const std::string& lowerName) const {
        auto it = headers.find(lowerName);
        return it == headers.end() ? "" : it->second;
    }
    bool ok() const { return status >= 200 && status < 300; }
};

struct HttpRequest {
    std::string method = "GET", url, body, contentType;
    std::map<std::string, std::string> headers;
    int timeoutSeconds = 20;
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse send(const HttpRequest& req) = 0;
    // Streams the body to onData as it arrives (AI answers typing live). The default just waits for the
    // whole body; the Qt client overrides it. onData returning false cancels the request.
    virtual HttpResponse stream(const HttpRequest& req, const std::function<bool(const std::string&)>& onData) {
        HttpResponse r = send(req);
        if (!r.body.empty()) onData(r.body);
        return r;
    }
    HttpResponse get(const std::string& url, std::map<std::string, std::string> headers = {}, int timeout = 20) {
        HttpRequest r;
        r.url = url;
        r.headers = std::move(headers);
        r.timeoutSeconds = timeout;
        return send(r);
    }
    HttpResponse postJson(const std::string& url, const std::string& json, int timeout = 20) {
        HttpRequest r;
        r.method = "POST";
        r.url = url;
        r.body = json;
        r.contentType = "application/json";
        r.timeoutSeconds = timeout;
        return send(r);
    }
};

}  // namespace hunter
