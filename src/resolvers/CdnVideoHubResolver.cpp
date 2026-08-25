#include "wannaviewer/resolvers/CdnVideoHubResolver.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <regex>
#include <unordered_map>

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
    ReplaceAll(value, "&apos;", "'");
    ReplaceAll(value, "&#39;", "'");
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

std::string CleanPath(const Url& url) {
    return url.Path().substr(0, url.Path().find_first_of("?#"));
}

std::string Lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool DigitsOnly(std::string_view value) {
    return !value.empty() && value.size() <= 24 &&
           std::ranges::all_of(value, [](unsigned char character) { return std::isdigit(character); });
}

std::string JsonString(const nlohmann::json& object, const char* key) {
    const auto value = object.find(key);
    if (value == object.end()) return {};
    if (value->is_string()) return value->get<std::string>();
    if (value->is_number_integer()) return std::to_string(value->get<std::int64_t>());
    return {};
}

int HexValue(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

std::string UrlDecode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '+') result.push_back(' ');
        else if (value[index] == '%' && index + 2 < value.size()) {
            const int high = HexValue(value[index + 1]);
            const int low = HexValue(value[index + 2]);
            if (high < 0 || low < 0) return {};
            result.push_back(static_cast<char>((high << 4) | low));
            index += 2;
        } else result.push_back(value[index]);
    }
    return result;
}

std::string QueryParameter(const Url& url, std::string_view wanted) {
    const auto question = url.Path().find('?');
    if (question == std::string::npos) return {};
    const auto fragment = url.Path().find('#', question + 1);
    const auto query = std::string_view(url.Path()).substr(question + 1, fragment - (question + 1));
    for (std::size_t start = 0; start <= query.size(); ) {
        const auto end = query.find('&', start);
        const auto item = query.substr(start, end - start);
        const auto equals = item.find('=');
        if (UrlDecode(item.substr(0, equals)) == wanted)
            return equals == std::string_view::npos ? std::string{} : UrlDecode(item.substr(equals + 1));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return {};
}

std::string QueryEncode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' || character == '.') result.push_back(static_cast<char>(character));
        else {
            result.push_back('%');
            result.push_back(hex[character >> 4]);
            result.push_back(hex[character & 15]);
        }
    }
    return result;
}

HeaderMap PlaybackHeaders(const HeaderMap& input, std::string_view fallbackReferer) {
    HeaderMap result;
    for (const auto& [name, value] : input) {
        const auto lower = Lower(name);
        // OK CDN rejects otherwise valid signed CVH segment URLs when the iframe
        // Origin is forwarded to the media CDN. Referer and User-Agent are safe
        // to preserve, but Origin is scoped to the provider API request only.
        if (lower == "referer" || lower == "user-agent") result.insert_or_assign(name, value);
    }
    bool hasReferer = false;
    for (const auto& [name, value] : result) {
        (void)value;
        if (Lower(name) == "referer") hasReferer = true;
    }
    if (!hasReferer && Url::Parse(fallbackReferer)) result.emplace("Referer", fallbackReferer);
    return result;
}

struct EmbedConfiguration final {
    std::string titleId;
    std::string publisherId;
    std::string aggregator;
    std::string episode;
    std::string voice;
};

EmbedConfiguration ConfigurationFor(const Url& url, std::string_view html) {
    EmbedConfiguration result;
    if (url.HostIs("yummyani.me") && CleanPath(url) == "/iframeCVH.html") {
        result.titleId = QueryParameter(url, "anime_id");
        result.publisherId = "745";
        result.aggregator = "mali";
        result.episode = QueryParameter(url, "episode");
        result.voice = QueryParameter(url, "dubbing_code");
        return result;
    }
    const std::string source(html);
    const std::regex playerExpression(R"(<video-player\b([^>]*)>)", std::regex::icase);
    std::smatch player;
    if (!std::regex_search(source, player, playerExpression)) return result;
    const auto attributes = player[1].str();
    result.titleId = Attribute(attributes, "data-title-id");
    result.publisherId = Attribute(attributes, "data-publisher-id");
    result.aggregator = Attribute(attributes, "data-aggregator");
    result.episode = Attribute(attributes, "episode");
    result.voice = Attribute(attributes, "priority-voice");
    return result;
}

} // namespace

bool CdnVideoHubResolver::CanHandle(const Url& url) const {
    const auto path = CleanPath(url);
    if (url.HostIs("plapi.cdnvideohub.com")) return path.starts_with("/api/v1/player/sv/video/");
    if (url.HostIs("animego.me")) return path.starts_with("/cdn-iframe/");
    return url.HostIs("yummyani.me") && path == "/iframeCVH.html";
}

ResolveResult CdnVideoHubResolver::Resolve(const Url& url, const ResolveContext& context) const {
    const auto path = CleanPath(url);
    if (url.HostIs("plapi.cdnvideohub.com") && path.starts_with("/api/v1/player/sv/video/")) {
        HttpRequest request{url};
        request.headers = context.requestHeaders;
        request.headers.insert_or_assign("Accept", "application/json");
        request.maximumResponseBytes = 512U * 1024U;
        const auto response = context.http.Get(request, context.stopToken);
        if (response.status < 200 || response.status >= 300)
            return {ResolveStatus::Failed, "cdnvideohub", "CVH stream request returned HTTP " +
                        std::to_string(response.status), {}};
        return ParseSourceFixture(response.body, response.finalUrl.empty() ? url.Value() : response.finalUrl,
                                  PlaybackHeaders(context.requestHeaders, url.Value()));
    }

    HttpRequest embedRequest{url};
    embedRequest.headers = context.requestHeaders;
    embedRequest.headers.insert_or_assign("Accept", "text/html,application/xhtml+xml");
    embedRequest.maximumResponseBytes = 2U * 1024U * 1024U;
    const auto embed = context.http.Get(embedRequest, context.stopToken);
    if (embed.status < 200 || embed.status >= 300)
        return {ResolveStatus::Failed, "cdnvideohub", "CVH embed returned HTTP " + std::to_string(embed.status), {}};
    const auto configuration = ConfigurationFor(url, embed.body);
    if (!DigitsOnly(configuration.titleId) || !DigitsOnly(configuration.publisherId) ||
        configuration.aggregator.empty() || configuration.aggregator.size() > 64)
        return {ResolveStatus::Unsupported, "cdnvideohub", "CVH embed configuration is incomplete", {}};

    const auto endpointText = "https://plapi.cdnvideohub.com/api/v1/player/sv/playlist?pub=" +
                              configuration.publisherId + "&id=" + configuration.titleId +
                              "&aggr=" + QueryEncode(configuration.aggregator);
    const auto endpoint = Url::Parse(endpointText);
    if (!endpoint) return {ResolveStatus::Failed, "cdnvideohub", "CVH playlist URL is invalid", {}};
    HttpRequest playlistRequest{*endpoint};
    playlistRequest.headers.emplace("Accept", "application/json");
    playlistRequest.headers.emplace("Referer", url.Value());
    playlistRequest.maximumResponseBytes = 2U * 1024U * 1024U;
    const auto playlist = context.http.Get(playlistRequest, context.stopToken);
    if (playlist.status == 204)
        return {ResolveStatus::Unsupported, "cdnvideohub", "CVH has no public stream for this selection", {}};
    if (playlist.status < 200 || playlist.status >= 300)
        return {ResolveStatus::Failed, "cdnvideohub", "CVH playlist returned HTTP " +
                    std::to_string(playlist.status), {}};
    return ParsePlaylistFixture(playlist.body, url.Value(), configuration.episode, configuration.voice);
}

ResolveResult CdnVideoHubResolver::ParsePlaylistFixture(std::string_view json, std::string_view sourceUrl,
                                                        std::string_view episodeHint,
                                                        std::string_view voiceHint) const {
    const auto source = Url::Parse(sourceUrl);
    const auto document = nlohmann::json::parse(json, nullptr, false, true);
    if (!source || document.is_discarded() || !document.is_object())
        return {ResolveStatus::Failed, "cdnvideohub", "CVH returned invalid playlist JSON", {}};
    const auto items = document.find("items");
    if (items == document.end() || !items->is_array())
        return {ResolveStatus::Unsupported, "cdnvideohub", "CVH playlist contains no items", {}};

    ResolveResult result{ResolveStatus::Resolved, "cdnvideohub", {}, {"CVH", {}}};
    std::unordered_map<std::string, std::size_t> seasonIndexes;
    std::vector<std::unordered_map<std::string, std::size_t>> voiceIndexes;
    const auto wantedVoice = Lower(std::string(voiceHint));
    const HeaderMap headers{{"Referer", std::string(sourceUrl)}};
    std::size_t accepted = 0;
    for (const auto& item : *items) {
        if (!item.is_object() || accepted >= 64) continue;
        const auto videoId = JsonString(item, "vkId");
        const auto episode = JsonString(item, "episode");
        const auto season = JsonString(item, "season");
        auto voice = JsonString(item, "voiceStudio");
        const auto voiceType = JsonString(item, "voiceType");
        if (!DigitsOnly(videoId) || episode.empty()) continue;
        if (!episodeHint.empty() && episode != episodeHint) continue;
        if (!wantedVoice.empty() && Lower(voice) != wantedVoice) continue;
        if (voice.empty()) voice = voiceType.empty() ? "Default" : voiceType;
        const auto seasonId = season.empty() ? "default" : season;
        auto [seasonPosition, newSeason] = seasonIndexes.emplace(seasonId, result.entry.seasons.size());
        if (newSeason) {
            result.entry.seasons.push_back({seasonId, season.empty() ? "Default" : "Season " + season, {}});
            voiceIndexes.emplace_back();
        }
        auto& seasonResult = result.entry.seasons[seasonPosition->second];
        auto& indexes = voiceIndexes[seasonPosition->second];
        auto [voicePosition, newVoice] = indexes.emplace(voice, seasonResult.voiceTracks.size());
        if (newVoice) seasonResult.voiceTracks.push_back({voice, voice, {}});
        auto& voiceResult = seasonResult.voiceTracks[voicePosition->second];
        const auto existing = std::ranges::find_if(voiceResult.episodes, [&](const Episode& value) {
            return value.id == episode;
        });
        auto* episodeResult = existing == voiceResult.episodes.end() ? nullptr : &*existing;
        if (!episodeResult) {
            voiceResult.episodes.push_back({episode, "Episode " + episode, {}});
            episodeResult = &voiceResult.episodes.back();
        }
        const auto endpoint = "https://plapi.cdnvideohub.com/api/v1/player/sv/video/" + videoId;
        episodeResult->streams.push_back({endpoint, {}, "CVH", {}, "embed", headers, false});
        ++accepted;
    }
    if (accepted == 0) {
        if (!voiceHint.empty())
            return {ResolveStatus::Unsupported, "cdnvideohub",
                    "CVH no longer has the selected episode/voice combination", {}};
        return {ResolveStatus::Unsupported, "cdnvideohub", "CVH returned no playable public items", {}};
    }
    return result;
}

ResolveResult CdnVideoHubResolver::ParseSourceFixture(std::string_view json, std::string_view sourceUrl,
                                                      HeaderMap headers) const {
    headers = PlaybackHeaders(headers, sourceUrl);
    const auto document = nlohmann::json::parse(json, nullptr, false, true);
    if (document.is_discarded() || !document.is_object())
        return {ResolveStatus::Failed, "cdnvideohub", "CVH returned invalid stream JSON", {}};
    const auto sources = document.find("sources");
    if (sources == document.end() || !sources->is_object())
        return {ResolveStatus::Unsupported, "cdnvideohub", "CVH returned no public media sources", {}};
    auto mediaUrl = JsonString(*sources, "hlsUrl");
    auto protocol = std::string("hls");
    if (!Url::Parse(mediaUrl)) {
        mediaUrl = JsonString(*sources, "dashUrl");
        protocol = "dash";
    }
    if (!Url::Parse(mediaUrl))
        return {ResolveStatus::Unsupported, "cdnvideohub", "CVH returned no valid HLS/DASH source", {}};
    ResolveResult result{ResolveStatus::Resolved, "cdnvideohub", {}, {"CVH stream", {}}};
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
