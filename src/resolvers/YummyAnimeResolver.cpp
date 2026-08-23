#include "wannaviewer/resolvers/YummyAnimeResolver.hpp"

#include <regex>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace wannaviewer {
namespace {

std::string StringValue(const nlohmann::json& item, std::initializer_list<const char*> keys,
                        std::string fallback = {}) {
    for (const auto* key : keys) {
        const auto value = item.find(key);
        if (value != item.end() && value->is_string()) return value->get<std::string>();
        if (value != item.end() && value->is_number_integer()) return std::to_string(value->get<std::int64_t>());
    }
    return fallback;
}

const nlohmann::json* FindCatalog(const nlohmann::json& node, unsigned depth = 0) {
    if (depth > 32) return nullptr;
    if (node.is_object()) {
        if ((node.contains("seasons") || node.contains("episodes")) &&
            (node.contains("title") || node.contains("name"))) return &node;
        for (const auto& [key, value] : node.items()) {
            (void)key;
            if (const auto* found = FindCatalog(value, depth + 1)) return found;
        }
    } else if (node.is_array()) {
        for (const auto& value : node) if (const auto* found = FindCatalog(value, depth + 1)) return found;
    }
    return nullptr;
}

std::vector<StreamVariant> ParseStreams(const nlohmann::json& episode) {
    const nlohmann::json* list = nullptr;
    for (const auto* key : {"streams", "sources", "videos", "providers"}) {
        const auto found = episode.find(key);
        if (found != episode.end() && found->is_array()) { list = &*found; break; }
    }
    std::vector<StreamVariant> streams;
    if (!list) return streams;
    for (const auto& source : *list) {
        if (!source.is_object()) continue;
        auto value = StringValue(source, {"url", "src", "file", "embedUrl"});
        if (!Url::Parse(value)) continue;
        const bool protectedStream = source.value("drm", false) || source.contains("widevine") || source.contains("fairplay");
        auto protocol = StringValue(source, {"protocol", "type"});
        if (protocol.empty()) {
            if (value.find(".m3u8") != std::string::npos) protocol = "hls";
            else if (value.find(".mpd") != std::string::npos) protocol = "dash";
            else if (value.find(".mp4") != std::string::npos) protocol = "http";
            else protocol = "embed";
        }
        HeaderMap headers;
        if (const auto headerObject = source.find("headers"); headerObject != source.end() && headerObject->is_object()) {
            for (const auto& [name, header] : headerObject->items())
                if (header.is_string()) headers.emplace(name, header.get<std::string>());
        }
        streams.push_back({std::move(value), {}, StringValue(source, {"quality", "label", "resolution"}, "Auto"),
                           StringValue(source, {"codec", "vcodec"}), std::move(protocol), std::move(headers), protectedStream});
    }
    return streams;
}

VoiceTrack ParseVoice(const nlohmann::json& voice, std::size_t index) {
    VoiceTrack result{StringValue(voice, {"id", "slug"}, std::to_string(index + 1)),
                      StringValue(voice, {"title", "name", "translation"}, "Default"), {}};
    const auto episodes = voice.find("episodes");
    if (episodes != voice.end() && episodes->is_array()) {
        std::size_t episodeIndex = 0;
        for (const auto& episode : *episodes) {
            if (!episode.is_object()) continue;
            ++episodeIndex;
            result.episodes.push_back({StringValue(episode, {"id", "number"}, std::to_string(episodeIndex)),
                                       StringValue(episode, {"title", "name", "number"}, "Episode " + std::to_string(episodeIndex)),
                                       ParseStreams(episode)});
        }
    } else if (episodes != voice.end() && episodes->is_object()) {
        const auto count = episodes->value("count", 0U);
        const auto aired = std::min(count, episodes->value("aired", count));
        for (unsigned episode = 1; episode <= aired && episode <= 1000; ++episode)
            result.episodes.push_back({std::to_string(episode), "Episode " + std::to_string(episode), {}});
    }
    return result;
}

AnimeEntry ParseCatalog(const nlohmann::json& catalog) {
    AnimeEntry entry{StringValue(catalog, {"title", "name"}, "Anime"), {}};
    const auto seasons = catalog.find("seasons");
    if (seasons != catalog.end() && seasons->is_array()) {
        std::size_t seasonIndex = 0;
        for (const auto& season : *seasons) {
            if (!season.is_object()) continue;
            ++seasonIndex;
            Season result{StringValue(season, {"id", "slug"}, std::to_string(seasonIndex)),
                          StringValue(season, {"title", "name"}, "Season " + std::to_string(seasonIndex)), {}};
            const nlohmann::json* voices = nullptr;
            for (const auto* key : {"voiceTracks", "translations", "dubs"}) {
                const auto found = season.find(key);
                if (found != season.end() && found->is_array()) { voices = &*found; break; }
            }
            if (voices) {
                std::size_t voiceIndex = 0;
                for (const auto& voice : *voices) if (voice.is_object()) result.voiceTracks.push_back(ParseVoice(voice, voiceIndex++));
            } else if (season.contains("episodes")) {
                result.voiceTracks.push_back(ParseVoice(season, 0));
            }
            entry.seasons.push_back(std::move(result));
        }
    } else if (catalog.contains("episodes")) {
        entry.seasons.push_back({"1", "Season 1", {ParseVoice(catalog, 0)}});
    }
    return entry;
}

std::string ExtractTitle(std::string_view html) {
    const std::string source(html);
    const std::regex expression(R"(<meta[^>]+(?:property|name)\s*=\s*["'](?:og:title|twitter:title)["'][^>]+content\s*=\s*["']([^"']+))", std::regex::icase);
    std::smatch match;
    return std::regex_search(source, match, expression) ? match[1].str() : "Anime";
}

} // namespace

bool YummyAnimeResolver::CanHandle(const Url& url) const {
    return url.HostIs("yummyanime.tv") || url.HostIs("yummyani.me");
}

ResolveResult YummyAnimeResolver::Resolve(const Url& url, const ResolveContext& context) const {
    HttpRequest request{url};
    request.headers.emplace("Accept", "text/html,application/xhtml+xml,application/json;q=0.9");
    const auto response = context.http.Get(request, context.stopToken);
    if (response.status < 200 || response.status >= 300)
        return {ResolveStatus::Failed, "yummyanime", "Page returned HTTP " + std::to_string(response.status), {}};
    auto result = ParseFixture(response.body, response.finalUrl.empty() ? url.Value() : response.finalUrl);

    // The legacy site exposes stable semantic provider metadata in data-params.
    // Resolve only its public controller response; provider pages remain separate resolver stages.
    const std::regex providerExpression(R"(data-params\s*=\s*["']([^"']+)["'])", std::regex::icase);
    std::vector<StreamVariant> providers;
    std::unordered_map<std::string, bool> seen;
    const std::string html(response.body);
    for (auto item = std::sregex_iterator(html.begin(), html.end(), providerExpression);
         item != std::sregex_iterator() && providers.size() < 16; ++item) {
        auto parameters = (*item)[1].str();
        for (std::size_t position = 0; (position = parameters.find("&amp;", position)) != std::string::npos; )
            parameters.replace(position, 5, "&");
        if (parameters.find("mod=") == std::string::npos || parameters.find("id=") == std::string::npos) continue;
        const auto endpointText = url.Scheme() + "://" + url.Host() + "/engine/ajax/controller.php?" + parameters;
        const auto endpoint = Url::Parse(endpointText);
        if (!endpoint || seen.contains(endpointText)) continue;
        seen.emplace(endpointText, true);
        try {
            HttpRequest providerRequest{*endpoint};
            providerRequest.headers.emplace("Referer", url.Value());
            providerRequest.maximumResponseBytes = 512U * 1024U;
            const auto providerResponse = context.http.Get(providerRequest, context.stopToken);
            if (providerResponse.status < 200 || providerResponse.status >= 300) continue;
            auto document = nlohmann::json::parse(providerResponse.body, nullptr, false, true);
            if (document.is_discarded() || !document.value("success", false)) continue;
            const auto embedUrl = StringValue(document, {"data", "url"});
            if (!Url::Parse(embedUrl)) continue;
            const auto modPosition = parameters.find("mod=");
            const auto modEnd = parameters.find('&', modPosition);
            const auto providerName = parameters.substr(modPosition + 4, modEnd - (modPosition + 4));
            HeaderMap headers{{"Referer", url.Value()}, {"Origin", url.Scheme() + "://" + url.Host()}};
            providers.push_back({embedUrl, {}, providerName, {}, "embed", std::move(headers), false});
        } catch (const std::exception& error) {
            context.logger.Write(LogLevel::Debug, "yummyanime", std::string("Provider metadata endpoint failed: ") + error.what());
        }
    }
    if (!providers.empty()) {
        if (result.entry.title.empty()) result.entry.title = ExtractTitle(response.body);
        Season season{"default", "Default", {}};
        VoiceTrack providersTrack{"providers", "Provider choices", {}};
        providersTrack.episodes.push_back({"catalog", "Catalog", std::move(providers)});
        season.voiceTracks.push_back(std::move(providersTrack));
        result.entry.seasons.push_back(std::move(season));
        result.status = ResolveStatus::Resolved;
        result.resolverId = "yummyanime";
        result.message.clear();
    }
    return result;
}

ResolveResult YummyAnimeResolver::ParseFixture(std::string_view html, std::string_view) const {
    const std::string source(html);
    const std::regex scriptExpression(R"(<script[^>]*(?:type\s*=\s*["']application/(?:ld\+)?json["']|id\s*=\s*["']__NEXT_DATA__["'])[^>]*>([\s\S]*?)</script>)", std::regex::icase);
    for (auto item = std::sregex_iterator(source.begin(), source.end(), scriptExpression);
         item != std::sregex_iterator(); ++item) {
        auto document = nlohmann::json::parse((*item)[1].str(), nullptr, false, true);
        if (document.is_discarded()) continue;
        const auto* catalog = FindCatalog(document);
        if (!catalog) continue;
        auto entry = ParseCatalog(*catalog);
        if (entry.title.empty()) entry.title = ExtractTitle(html);
        if (!entry.seasons.empty()) return {ResolveStatus::Resolved, "yummyanime", {}, std::move(entry)};
    }
    const std::regex hydrationExpression(R"HYD(window\.__staticRouterHydrationData\s*=\s*JSON\.parse\("((?:\\.|[^"\\])*)"\))HYD", std::regex::icase);
    std::smatch hydration;
    if (std::regex_search(source, hydration, hydrationExpression)) {
        auto encoded = nlohmann::json::parse('"' + hydration[1].str() + '"', nullptr, false, true);
        if (encoded.is_string()) {
            auto document = nlohmann::json::parse(encoded.get<std::string>(), nullptr, false, true);
            if (!document.is_discarded()) {
                if (const auto* catalog = FindCatalog(document)) {
                    auto entry = ParseCatalog(*catalog);
                    if (!entry.seasons.empty()) return {ResolveStatus::Resolved, "yummyanime", {}, std::move(entry)};
                }
            }
        }
    }
    return {ResolveStatus::Unsupported, "yummyanime",
            "Public episode metadata was not present in static HTML; a browser resolver may be required", {ExtractTitle(html), {}}};
}

} // namespace wannaviewer
