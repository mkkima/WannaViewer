#pragma once

#include "wannaviewer/core/Url.hpp"

#include <chrono>
#include <cstddef>
#include <stop_token>
#include <string>
#include <unordered_map>

namespace wannaviewer {

using HeaderMap = std::unordered_map<std::string, std::string>;

struct HttpRequest final {
    Url url;
    HeaderMap headers;
    std::chrono::milliseconds connectTimeout{10'000};
    std::chrono::milliseconds receiveTimeout{20'000};
    std::size_t maximumResponseBytes{8U * 1024U * 1024U};
    unsigned maximumRedirects{5};
};

struct HttpResponse final {
    unsigned status{0};
    std::string finalUrl;
    std::string contentType;
    std::string body;
};

class HttpClient final {
public:
    [[nodiscard]] HttpResponse Get(const HttpRequest& request, std::stop_token stopToken = {}) const;
};

} // namespace wannaviewer
