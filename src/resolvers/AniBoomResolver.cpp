#include "wannaviewer/resolvers/AniBoomResolver.hpp"

#include <regex>

namespace wannaviewer {
namespace {

void ReplaceAll(std::string& value, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    for (std::size_t position = 0; (position = value.find(from, position)) != std::string::npos; ) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string NormalizeDocument(std::string value) {
    ReplaceAll(value, "&quot;", "\"");
    ReplaceAll(value, "&#34;", "\"");
    ReplaceAll(value, "&#x22;", "\"");
    ReplaceAll(value, "&amp;", "&");
    ReplaceAll(value, "&#38;", "&");
    ReplaceAll(value, "\\u0026", "&");
    for (unsigned pass = 0; pass < 4; ++pass) ReplaceAll(value, "\\/", "/");
    return value;
}

std::string Origin(const Url& url) { return url.Scheme() + "://" + url.Host(); }

} // namespace

bool AniBoomResolver::CanHandle(const Url& url) const {
    auto path = url.Path().substr(0, url.Path().find_first_of("?#"));
    return url.HostIs("aniboom.one") && path.starts_with("/embed/");
}

ResolveResult AniBoomResolver::Resolve(const Url& url, const ResolveContext& context) const {
    HttpRequest request{url};
    request.headers = context.requestHeaders;
    request.headers.insert_or_assign("Accept", "text/html,application/xhtml+xml");
    request.maximumResponseBytes = 4U * 1024U * 1024U;
    const auto response = context.http.Get(request, context.stopToken);
    if (response.status < 200 || response.status >= 300)
        return {ResolveStatus::Failed, "aniboom", "AniBoom returned HTTP " + std::to_string(response.status), {}};
    return ParseFixture(response.body, response.finalUrl.empty() ? url.Value() : response.finalUrl);
}

ResolveResult AniBoomResolver::ParseFixture(std::string_view html, std::string_view sourceUrl) const {
    const auto source = Url::Parse(sourceUrl);
    if (!source) return {ResolveStatus::Failed, "aniboom", "AniBoom source URL is invalid", {}};
    const auto normalized = NormalizeDocument(std::string(html));
    const std::regex hlsExpression(
        R"((https?://[^"'<>\s\\]+\.m3u8(?:\?[^"'<>\s\\]*)?))", std::regex::icase);
    const std::regex dashExpression(
        R"((https?://[^"'<>\s\\]+\.mpd(?:\?[^"'<>\s\\]*)?))", std::regex::icase);
    std::smatch match;
    std::string mediaUrl;
    std::string protocol;
    if (std::regex_search(normalized, match, hlsExpression)) {
        mediaUrl = match[1].str();
        protocol = "hls";
    } else if (std::regex_search(normalized, match, dashExpression)) {
        mediaUrl = match[1].str();
        protocol = "dash";
    }
    if (!Url::Parse(mediaUrl))
        return {ResolveStatus::Unsupported, "aniboom",
                "AniBoom did not publish an unprotected HLS/DASH stream", {}};

    HeaderMap headers{{"Referer", std::string(sourceUrl)}, {"Origin", Origin(*source)}};
    ResolveResult result{ResolveStatus::Resolved, "aniboom", {}, {"AniBoom", {}}};
    Season season{"default", "Default", {}};
    VoiceTrack voice{"default", "Default", {}};
    voice.episodes.push_back({"default", "Video",
                              {{std::move(mediaUrl), {}, "Auto", {}, std::move(protocol),
                                std::move(headers), false}}});
    season.voiceTracks.push_back(std::move(voice));
    result.entry.seasons.push_back(std::move(season));
    return result;
}

} // namespace wannaviewer
