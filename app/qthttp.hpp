// HttpClient on Qt Network. Blocking by design (the scanner runs on its own thread): each call
// spins a local event loop on the calling thread with its own QNetworkAccessManager.
#pragma once
#include "../core/http.hpp"

class QtHttpClient : public hunter::HttpClient {
public:
    hunter::HttpResponse send(const hunter::HttpRequest& req) override;
    hunter::HttpResponse stream(const hunter::HttpRequest& req,
                                const std::function<bool(const std::string&)>& onData) override;
};
