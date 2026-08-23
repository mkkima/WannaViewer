#pragma once

#include "wannaviewer/network/Resolver.hpp"

#include <memory>
#include <vector>

namespace wannaviewer {

class YtDlpBridge;

class ResolverPipeline final {
public:
    explicit ResolverPipeline(const YtDlpBridge* ytDlp = nullptr);
    void Add(std::unique_ptr<IPageResolver> resolver);
    [[nodiscard]] ResolveResult Resolve(const Url& url, const ResolveContext& context) const;

private:
    std::vector<std::unique_ptr<IPageResolver>> resolvers_;
    const YtDlpBridge* ytDlp_{nullptr};
};

} // namespace wannaviewer
