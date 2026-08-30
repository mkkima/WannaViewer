#include "wannaviewer/resolvers/AnimeGoResolver.hpp"

#include <algorithm>
#include <regex>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace wannaviewer {
namespace {

void ReplaceAll(std::string& value, std::string_view from, std::string_view to) {
    if (from.empty()) return;
    for (std::size_t position = 0; (position = value.find(from, position)) != std::string::npos; ) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

std::string DecodeHtml(std::string value) {
    ReplaceAll(value, "&quot;", "\"");
    ReplaceAll(value, "&#34;", "\"");
    ReplaceAll(value, "&#x22;", "\"");
    ReplaceAll(value, "&apos;", "'");
    ReplaceAll(value, "&#39;", "'");
    ReplaceAll(value, "&lt;", "<");
    ReplaceAll(value, "&gt;", ">");
    ReplaceAll(value, "&amp;", "&");
    return value;
}

std::string Attribute(std::string_view attributes, std::string_view name) {
    const std::string source(attributes);
    const std::regex expression("(?:^|\\s)" + std::string(name) + R"(\s*=\s*["']([^"']*)["'])",
                                std::regex::icase);
    std::smatch match;
    return std::regex_search(source, match, expression) ? DecodeHtml(match[1].str()) : std::string{};
}

std::string StripTags(std::string value) {
    value = std::regex_replace(value, std::regex(R"(<[^>]*>)"), "");
    value = DecodeHtml(std::move(value));
    value = std::regex_replace(value, std::regex(R"(\s+)"), " ");
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    while (!value.empty() && value.back() == ' ') value.pop_back();
    return value;
}

std::string PageTitle(std::string_view html) {
    const std::string source(html);
    for (const auto& expression : {
             std::regex(R"(<meta[^>]+property\s*=\s*["']og:title["'][^>]+content\s*=\s*["']([^"']+))",
                        std::regex::icase),
             std::regex(R"(<title[^>]*>([\s\S]*?)</title>)", std::regex::icase)}) {
        std::smatch match;
        if (std::regex_search(source, match, expression)) return StripTags(match[1].str());
    }
    return "AnimeGo";
}

std::string CleanPath(const Url& url) {
    const auto end = url.Path().find_first_of("?#");
    return url.Path().substr(0, end);
}

std::string Origin(const Url& url) { return url.Scheme() + "://" + url.Host(); }

std::string AbsoluteUrl(std::string value, const Url& base) {
    value = DecodeHtml(std::move(value));
    if (value.starts_with("//")) return base.Scheme() + ':' + value;
    if (value.starts_with('/')) return Origin(base) + value;
    return value;
}

bool IsSupportedProvider(const Url& url) {
    if (url.HostIs("aniboom.one")) return CleanPath(url).starts_with("/embed/");
    if (url.HostIs("animego.me")) return CleanPath(url).starts_with("/cdn-iframe/");
    return url.HostIs("player.cdnvideohub.com") || url.HostIs("kodikplayer.com");
}

std::string JsonPlayerContent(std::string_view playerJson) {
    const auto document = nlohmann::json::parse(playerJson, nullptr, false, true);
    if (document.is_discarded() || !document.is_object() || document.value("status", "") != "success") return {};
    const auto data = document.find("data");
    if (data == document.end() || !data->is_object()) return {};
    const auto content = data->find("content");
    return content != data->end() && content->is_string() ? content->get<std::string>() : std::string{};
}

std::string RefererFrom(const ResolveContext& context, const Url& fallback) {
    for (const auto& [name, value] : context.requestHeaders) {
        std::string lower = name;
        std::ranges::transform(lower, lower.begin(), [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        if (lower == "referer" && Url::Parse(value)) return value;
    }
    return fallback.Value();
}

} // namespace

bool AnimeGoResolver::CanHandle(const Url& url) const {
    if (!url.HostIs("animego.me")) return false;
    const auto path = CleanPath(url);
    return path.starts_with("/anime/") || path.starts_with("/player/videos/");
}

ResolveResult AnimeGoResolver::Resolve(const Url& url, const ResolveContext& context) const {
    const auto path = CleanPath(url);
    if (path.starts_with("/player/videos/")) {
        const auto referer = RefererFrom(context, url);
        HttpRequest request{url};
        request.headers = context.requestHeaders;
        request.headers.insert_or_assign("Accept", "application/json");
        request.headers.insert_or_assign("X-Requested-With", "XMLHttpRequest");
        request.headers.insert_or_assign("Referer", referer);
        request.maximumResponseBytes = 2U * 1024U * 1024U;
        const auto response = context.http.Get(request, context.stopToken);
        if (response.status < 200 || response.status >= 300)
            return {ResolveStatus::Failed, "animego", "AnimeGo episode request returned HTTP " +
                        std::to_string(response.status), {}};
        return ParseEpisodeFixture(response.body, referer);
    }

    HttpRequest pageRequest{url};
    pageRequest.headers.emplace("Accept", "text/html,application/xhtml+xml");
    pageRequest.maximumResponseBytes = 4U * 1024U * 1024U;
    const auto page = context.http.Get(pageRequest, context.stopToken);
    if (page.status < 200 || page.status >= 300)
        return {ResolveStatus::Failed, "animego", "AnimeGo page returned HTTP " + std::to_string(page.status), {}};

    const std::regex loaderExpression(
        R"(data-anime-player-loader-url-value\s*=\s*["']([^"']+)["'])", std::regex::icase);
    std::smatch loader;
    if (!std::regex_search(page.body, loader, loaderExpression))
        return {ResolveStatus::Unsupported, "animego",
                "AnimeGo did not publish a player endpoint for this page", {PageTitle(page.body), {}}};

    const auto endpointText = AbsoluteUrl(loader[1].str(), url);
    const auto endpoint = Url::Parse(endpointText);
    if (!endpoint || !endpoint->HostIs("animego.me") || !CleanPath(*endpoint).starts_with("/player/"))
        return {ResolveStatus::Failed, "animego", "AnimeGo returned an invalid player endpoint", {}};

    HttpRequest playerRequest{*endpoint};
    playerRequest.headers.emplace("Accept", "application/json");
    playerRequest.headers.emplace("X-Requested-With", "XMLHttpRequest");
    playerRequest.headers.emplace("Referer", url.Value());
    playerRequest.maximumResponseBytes = 4U * 1024U * 1024U;
    const auto player = context.http.Get(playerRequest, context.stopToken);
    if (player.status < 200 || player.status >= 300)
        return {ResolveStatus::Failed, "animego", "AnimeGo player request returned HTTP " +
                    std::to_string(player.status), {}};
    return ParsePageFixture(page.body, player.body, url.Value());
}

ResolveResult AnimeGoResolver::ParsePageFixture(std::string_view pageHtml, std::string_view playerJson,
                                                std::string_view sourceUrl) const {
    const auto source = Url::Parse(sourceUrl);
    const auto content = JsonPlayerContent(playerJson);
    if (!source || content.empty())
        return {ResolveStatus::Unsupported, "animego", "AnimeGo returned an invalid player catalog", {}};

    ResolveResult result{ResolveStatus::Resolved, "animego", {}, {PageTitle(pageHtml), {}}};
    Season season{"available", "Available episodes", {}};
    VoiceTrack voice{"episodes", "Episodes", {}};
    const std::regex optionExpression(R"(<option\b([^>]*)>([\s\S]*?)</option>)", std::regex::icase);
    const HeaderMap headers{{"Referer", std::string(sourceUrl)}, {"Origin", Origin(*source)}};
    std::unordered_set<std::string> episodeIds;
    const auto appendEpisode = [&](std::string id, std::string title) {
        if (voice.episodes.size() >= 500 || id.empty() ||
            !std::ranges::all_of(id, [](unsigned char character) { return std::isdigit(character); }) ||
            !episodeIds.insert(id).second) return;
        if (title.empty()) title = "Episode " + std::to_string(voice.episodes.size() + 1);
        const auto endpoint = Origin(*source) + "/player/videos/" + id;
        voice.episodes.push_back({std::move(id), std::move(title),
                                  {{endpoint, {}, "Choose voice/player", {}, "embed", headers, false}}});
    };
    for (auto item = std::sregex_iterator(content.begin(), content.end(), optionExpression);
         item != std::sregex_iterator() && voice.episodes.size() < 500; ++item) {
        const auto id = Attribute((*item)[1].str(), "value");
        appendEpisode(id, StripTags((*item)[2].str()));
    }
    // Current AnimeGo catalogs publish episodes as carousel elements rather
    // than <option> nodes. Keep both formats so cached/older page variants
    // remain compatible and de-duplicate desktop/mobile representations.
    const std::regex episodeElementExpression(R"(<(?:div|button)\b([^>]*)>)", std::regex::icase);
    for (auto item = std::sregex_iterator(content.begin(), content.end(), episodeElementExpression);
         item != std::sregex_iterator() && voice.episodes.size() < 500; ++item) {
        const auto attributes = (*item)[1].str();
        const auto id = Attribute(attributes, "data-episode");
        const auto number = Attribute(attributes, "data-episode-number");
        appendEpisode(id, number.empty() ? std::string{} : "Episode " + number);
    }
    if (voice.episodes.empty())
        return {ResolveStatus::Unsupported, "animego", "AnimeGo returned no public episodes", {result.entry.title, {}}};
    season.voiceTracks.push_back(std::move(voice));
    result.entry.seasons.push_back(std::move(season));
    return result;
}

ResolveResult AnimeGoResolver::ParseEpisodeFixture(std::string_view playerJson,
                                                   std::string_view sourceUrl) const {
    const auto source = Url::Parse(sourceUrl);
    const auto content = JsonPlayerContent(playerJson);
    if (!source || content.empty())
        return {ResolveStatus::Unsupported, "animego", "AnimeGo returned invalid episode player data", {}};

    ResolveResult result{ResolveStatus::Resolved, "animego", {}, {"AnimeGo episode", {}}};
    Season season{"selected", "Selected episode", {}};
    std::unordered_map<std::string, std::size_t> voiceIndexes;
    const std::regex buttonExpression(R"(<button\b([^>]*)>)", std::regex::icase);
    const HeaderMap headers{{"Referer", std::string(sourceUrl)}, {"Origin", Origin(*source)}};
    for (auto item = std::sregex_iterator(content.begin(), content.end(), buttonExpression);
         item != std::sregex_iterator(); ++item) {
        const auto attributes = (*item)[1].str();
        if (Attribute(attributes, "data-anime-player-target") != "provider") continue;
        const auto candidateText = AbsoluteUrl(Attribute(attributes, "data-player"), *source);
        const auto candidate = Url::Parse(candidateText);
        if (!candidate || !IsSupportedProvider(*candidate)) continue;
        auto voiceTitle = Attribute(attributes, "data-translation-title");
        if (voiceTitle.empty()) voiceTitle = "Default";
        auto providerTitle = Attribute(attributes, "data-provider-title");
        if (providerTitle.empty()) providerTitle = candidate->Host();
        auto [position, inserted] = voiceIndexes.emplace(voiceTitle, season.voiceTracks.size());
        if (inserted) season.voiceTracks.push_back({voiceTitle, voiceTitle, {{"selected", "Selected episode", {}}}});
        auto& streams = season.voiceTracks[position->second].episodes.front().streams;
        if (streams.size() >= 16) continue;
        streams.push_back({candidateText, {}, std::move(providerTitle), {}, "embed", headers, false});
    }
    if (season.voiceTracks.empty())
        return {ResolveStatus::Unsupported, "animego",
                "This episode has no public CVH, AniBoom, or Kodik stream supported by WannaViewer", {}};
    result.entry.seasons.push_back(std::move(season));
    return result;
}

} // namespace wannaviewer
