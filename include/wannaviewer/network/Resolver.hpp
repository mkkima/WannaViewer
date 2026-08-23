#pragma once

#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/core/Url.hpp"
#include "wannaviewer/network/HttpClient.hpp"

#include <stop_token>
#include <string>
#include <vector>

namespace wannaviewer {

struct StreamVariant final {
    std::string url;
    std::string audioUrl;
    std::string quality;
    std::string codec;
    std::string protocol;
    HeaderMap headers;
    bool protectedStream{false};
};

struct Episode final {
    std::string id;
    std::string title;
    std::vector<StreamVariant> streams;
};

struct VoiceTrack final {
    std::string id;
    std::string title;
    std::vector<Episode> episodes;
};

struct Season final {
    std::string id;
    std::string title;
    std::vector<VoiceTrack> voiceTracks;
};

struct AnimeEntry final {
    std::string title;
    std::vector<Season> seasons;
};

enum class ResolveStatus { Resolved, Unsupported, Protected, Failed };

struct ResolveResult final {
    ResolveStatus status{ResolveStatus::Unsupported};
    std::string resolverId;
    std::string message;
    AnimeEntry entry;
};

struct ResolveContext final {
    const HttpClient& http;
    Logger& logger;
    std::stop_token stopToken;
    HeaderMap requestHeaders;
};

class IPageResolver {
public:
    [[nodiscard]] virtual std::string_view Id() const noexcept = 0;
    [[nodiscard]] virtual bool CanHandle(const Url& url) const = 0;
    [[nodiscard]] virtual ResolveResult Resolve(const Url& url, const ResolveContext& context) const = 0;
    virtual ~IPageResolver() = default;
};

} // namespace wannaviewer
