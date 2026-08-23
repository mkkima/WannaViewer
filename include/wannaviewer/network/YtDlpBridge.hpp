#pragma once

#include "wannaviewer/network/Resolver.hpp"

#include <filesystem>

namespace wannaviewer {

class YtDlpBridge final {
public:
    explicit YtDlpBridge(std::filesystem::path executable);
    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const;

private:
    std::filesystem::path executable_;
};

} // namespace wannaviewer
