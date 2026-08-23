#pragma once

#include "wannaviewer/network/Resolver.hpp"

namespace wannaviewer {

class GenericResolver final : public IPageResolver {
public:
    [[nodiscard]] std::string_view Id() const noexcept override { return "generic-html"; }
    [[nodiscard]] bool CanHandle(const Url& url) const override;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const override;
};

} // namespace wannaviewer
