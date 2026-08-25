#pragma once

#include "wannaviewer/network/Resolver.hpp"

namespace wannaviewer {

class CdnVideoHubResolver final : public IPageResolver {
public:
    [[nodiscard]] std::string_view Id() const noexcept override { return "cdnvideohub"; }
    [[nodiscard]] bool CanHandle(const Url& url) const override;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const override;

    [[nodiscard]] ResolveResult ParsePlaylistFixture(std::string_view json, std::string_view sourceUrl,
                                                     std::string_view episodeHint,
                                                     std::string_view voiceHint) const;
    [[nodiscard]] ResolveResult ParseSourceFixture(std::string_view json, std::string_view sourceUrl,
                                                   HeaderMap headers = {}) const;
};

} // namespace wannaviewer
