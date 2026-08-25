#pragma once

#include "wannaviewer/network/Resolver.hpp"

namespace wannaviewer {

class AnimeGoResolver final : public IPageResolver {
public:
    [[nodiscard]] std::string_view Id() const noexcept override { return "animego"; }
    [[nodiscard]] bool CanHandle(const Url& url) const override;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const override;

    [[nodiscard]] ResolveResult ParsePageFixture(std::string_view pageHtml, std::string_view playerJson,
                                                 std::string_view sourceUrl) const;
    [[nodiscard]] ResolveResult ParseEpisodeFixture(std::string_view playerJson,
                                                    std::string_view sourceUrl) const;
};

} // namespace wannaviewer
