#pragma once

#include "wannaviewer/network/Resolver.hpp"

namespace wannaviewer {

class AniBoomResolver final : public IPageResolver {
public:
    [[nodiscard]] std::string_view Id() const noexcept override { return "aniboom"; }
    [[nodiscard]] bool CanHandle(const Url& url) const override;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const override;
    [[nodiscard]] ResolveResult ParseFixture(std::string_view html, std::string_view sourceUrl) const;
};

} // namespace wannaviewer
