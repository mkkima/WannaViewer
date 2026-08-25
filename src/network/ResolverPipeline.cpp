#include "wannaviewer/network/ResolverPipeline.hpp"
#include "wannaviewer/network/YtDlpBridge.hpp"

#include <exception>
#include <optional>

namespace wannaviewer {

ResolverPipeline::ResolverPipeline(const YtDlpBridge* ytDlp) : ytDlp_(ytDlp) {}

void ResolverPipeline::Add(std::unique_ptr<IPageResolver> resolver) { resolvers_.push_back(std::move(resolver)); }

ResolveResult ResolverPipeline::Resolve(const Url& url, const ResolveContext& context) const {
    std::optional<ResolveResult> knownSiteFailure;
    for (const auto& resolver : resolvers_) {
        if (resolver->Id() == "generic-html") continue;
        if (!resolver->CanHandle(url)) continue;
        try {
            auto result = resolver->Resolve(url, context);
            if (result.status != ResolveStatus::Unsupported) return result;
            if (!result.message.empty()) knownSiteFailure = std::move(result);
        } catch (const std::exception& error) {
            context.logger.Write(LogLevel::Error, resolver->Id(), error.what());
            return {ResolveStatus::Failed, std::string(resolver->Id()), "Resolver failed; see the log for details", {}};
        }
    }
    if (ytDlp_ && ytDlp_->IsAvailable()) {
        auto result = ytDlp_->Resolve(url, context);
        if (result.status != ResolveStatus::Unsupported && result.status != ResolveStatus::Failed) return result;
    }
    if ((!ytDlp_ || !ytDlp_->IsAvailable()) &&
        (url.HostIs("youtube.com") || url.HostIs("youtu.be") || url.HostIs("vimeo.com")))
        return {ResolveStatus::Unsupported, "yt-dlp",
                "This site requires the yt-dlp helper. Local playback is unaffected.", {}};
    for (const auto& resolver : resolvers_) {
        if (resolver->Id() != "generic-html" || !resolver->CanHandle(url)) continue;
        try {
            auto result = resolver->Resolve(url, context);
            if (result.status == ResolveStatus::Unsupported && knownSiteFailure) return std::move(*knownSiteFailure);
            return result;
        } catch (const std::exception& error) {
            context.logger.Write(LogLevel::Error, resolver->Id(), error.what());
            return {ResolveStatus::Failed, std::string(resolver->Id()), "Resolver failed; see the log for details", {}};
        }
    }
    if (knownSiteFailure) return std::move(*knownSiteFailure);
    return {ResolveStatus::Unsupported, {}, "No resolver supports this URL", {}};
}

} // namespace wannaviewer
