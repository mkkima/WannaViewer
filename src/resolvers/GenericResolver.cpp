#include "wannaviewer/resolvers/GenericResolver.hpp"

#include <algorithm>
#include <regex>
#include <unordered_set>

namespace wannaviewer {
namespace {

std::string HtmlTitle(std::string_view html) {
    const std::string source(html);
    for (const auto& expression : {
             std::regex(R"(<meta[^>]+property\s*=\s*["']og:title["'][^>]+content\s*=\s*["']([^"']+))", std::regex::icase),
             std::regex(R"(<title[^>]*>\s*([^<]+))", std::regex::icase)}) {
        std::smatch match;
        if (std::regex_search(source, match, expression) && match.size() > 1) return match[1].str();
    }
    return "Web media";
}

std::string ProtocolFor(std::string_view url) {
    if (url.find(".m3u8") != std::string_view::npos) return "hls";
    if (url.find(".mpd") != std::string_view::npos) return "dash";
    return "http";
}

} // namespace

bool GenericResolver::CanHandle(const Url& url) const { return url.IsHttp() && !url.IsDirectMedia(); }

ResolveResult GenericResolver::Resolve(const Url& url, const ResolveContext& context) const {
    HttpRequest request{url};
    request.headers = context.requestHeaders;
    request.maximumResponseBytes = 8U * 1024U * 1024U;
    const auto response = context.http.Get(request, context.stopToken);
    if (response.status < 200 || response.status >= 300)
        return {ResolveStatus::Failed, "generic-html", "Page returned HTTP " + std::to_string(response.status), {}};

    static const std::regex mediaExpression(
        R"((?:contentUrl|src|href|file|url)["'\s:=]+(https?://[^"'<>\s]+\.(?:m3u8|mpd|mp4|mkv|webm|mov)(?:\?[^"'<>\s]*)?))",
        std::regex::icase);
    std::unordered_set<std::string> unique;
    std::vector<StreamVariant> streams;
    const std::string source(response.body);
    for (auto item = std::sregex_iterator(source.begin(), source.end(), mediaExpression);
         item != std::sregex_iterator() && streams.size() < 32; ++item) {
        auto candidate = (*item)[1].str();
        std::ranges::replace(candidate, '\\', '/');
        if (!Url::Parse(candidate) || !unique.insert(candidate).second) continue;
        streams.push_back({candidate, {}, "Auto", {}, ProtocolFor(candidate), context.requestHeaders, false});
    }
    if (streams.empty()) return {ResolveStatus::Unsupported, "generic-html", "No public media URL was found in static HTML", {}};

    ResolveResult result{ResolveStatus::Resolved, "generic-html", {}, {HtmlTitle(response.body), {}}};
    Season season{"default", "Default", {}};
    VoiceTrack voice{"default", "Default", {}};
    voice.episodes.push_back({"default", "Media", std::move(streams)});
    season.voiceTracks.push_back(std::move(voice));
    result.entry.seasons.push_back(std::move(season));
    return result;
}

} // namespace wannaviewer
