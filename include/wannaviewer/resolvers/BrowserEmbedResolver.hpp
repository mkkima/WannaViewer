#pragma once

#ifdef _WIN32

#include "wannaviewer/network/Resolver.hpp"

#include <filesystem>

namespace wannaviewer {

class BrowserEmbedResolver final : public IPageResolver {
public:
    explicit BrowserEmbedResolver(std::filesystem::path userDataFolder);

    [[nodiscard]] std::string_view Id() const noexcept override { return "browser-embed"; }
    [[nodiscard]] bool CanHandle(const Url& url) const override;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const override;

private:
    std::filesystem::path userDataFolder_;
};

} // namespace wannaviewer

#endif
