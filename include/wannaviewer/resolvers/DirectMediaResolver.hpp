#pragma once

#include "wannaviewer/network/Resolver.hpp"

namespace wannaviewer {

class DirectMediaResolver final : public IPageResolver {
public:
    [[nodiscard]] std::string_view Id() const noexcept override { return "direct-media"; }
    [[nodiscard]] bool CanHandle(const Url& url) const override;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const override;
};

} // namespace wannaviewer
