#include "wannaviewer/network/YtDlpBridge.hpp"
#include "wannaviewer/network/Process.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace wannaviewer {
namespace {

std::string JsonString(const nlohmann::json& object, const char* key) {
    const auto value = object.find(key);
    return value != object.end() && value->is_string() ? value->get<std::string>() : std::string{};
}

HeaderMap JsonHeaders(const nlohmann::json& object) {
    HeaderMap result;
    const auto headers = object.find("http_headers");
    if (headers == object.end() || !headers->is_object()) return result;
    for (const auto& [name, value] : headers->items()) if (value.is_string()) result.emplace(name, value.get<std::string>());
    return result;
}

} // namespace

YtDlpBridge::YtDlpBridge(std::filesystem::path executable) : executable_(std::move(executable)) {}
bool YtDlpBridge::IsAvailable() const noexcept { return std::filesystem::is_regular_file(executable_); }

ResolveResult YtDlpBridge::Resolve(const Url& url, const ResolveContext& context) const {
    if (!IsAvailable()) return {ResolveStatus::Unsupported, "yt-dlp", "This site requires the yt-dlp helper. Local playback is unaffected.", {}};
    std::vector<std::string> arguments{"--dump-single-json", "--skip-download", "--no-playlist", "--no-warnings",
                                       "--socket-timeout", "15"};
    for (const auto& [name, value] : context.requestHeaders) {
        if (name.find_first_of("\r\n:") != std::string::npos || value.find_first_of("\r\n") != std::string::npos) continue;
        std::string lower = name;
        std::ranges::transform(lower, lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lower == "cookie" || lower == "authorization" || lower == "proxy-authorization") continue;
        arguments.push_back("--add-header");
        arguments.push_back(name + ':' + value);
    }
    arguments.push_back("--");
    arguments.push_back(url.Value());
    const auto process = RunProcess(executable_, arguments,
                                    std::chrono::seconds(45), 32U * 1024U * 1024U, context.stopToken);
    if (process.cancelled) return {ResolveStatus::Failed, "yt-dlp", "Resolution cancelled", {}};
    if (process.timedOut) return {ResolveStatus::Failed, "yt-dlp", "yt-dlp timed out", {}};
    if (process.outputTruncated) return {ResolveStatus::Failed, "yt-dlp", "yt-dlp output exceeded the safe size limit", {}};
    if (process.exitCode != 0) {
        context.logger.Write(LogLevel::Error, "yt-dlp", process.output);
        return {ResolveStatus::Failed, "yt-dlp", "yt-dlp could not resolve this URL", {}};
    }
    auto document = nlohmann::json::parse(process.output, nullptr, false, true);
    if (document.is_discarded() || !document.is_object()) return {ResolveStatus::Failed, "yt-dlp", "yt-dlp returned invalid JSON", {}};

    auto title = JsonString(document, "title");
    if (title.empty()) title = "Internet video";
    std::vector<StreamVariant> streams;
    std::unordered_set<std::string> unique;
    const auto formats = document.find("formats");
    if (formats != document.end() && formats->is_array()) {
        std::string bestAudioUrl;
        double bestAudioBitrate = -1.0;
        for (const auto& format : *formats) {
            if (!format.is_object() || format.value("has_drm", false)) continue;
            const auto candidate = JsonString(format, "url");
            if (!Url::Parse(candidate) || JsonString(format, "vcodec") != "none" || JsonString(format, "acodec") == "none") continue;
            const double bitrate = format.value("abr", 0.0);
            if (bitrate > bestAudioBitrate) { bestAudioBitrate = bitrate; bestAudioUrl = candidate; }
        }
        for (const auto& format : *formats) {
            if (!format.is_object() || format.value("has_drm", false)) continue;
            const auto mediaUrl = JsonString(format, "url");
            if (!Url::Parse(mediaUrl) || !unique.insert(mediaUrl).second) continue;
            const auto videoCodec = JsonString(format, "vcodec");
            const auto audioCodec = JsonString(format, "acodec");
            if (videoCodec == "none") continue;
            const bool separateAudio = audioCodec == "none";
            if (separateAudio && bestAudioUrl.empty()) continue;
            auto quality = JsonString(format, "format_note");
            if (quality.empty() && format.contains("height") && format["height"].is_number_integer())
                quality = std::to_string(format["height"].get<int>()) + "p";
            if (quality.empty()) quality = JsonString(format, "format_id");
            streams.push_back({mediaUrl, separateAudio ? bestAudioUrl : std::string{}, std::move(quality), videoCodec,
                               JsonString(format, "protocol"), JsonHeaders(format), false});
        }
    }
    if (streams.empty()) {
        const auto mediaUrl = JsonString(document, "url");
        if (Url::Parse(mediaUrl) && !document.value("has_drm", false))
            streams.push_back({mediaUrl, {}, JsonString(document, "format_note"), JsonString(document, "vcodec"),
                               JsonString(document, "protocol"), JsonHeaders(document), false});
    }
    if (streams.empty()) return {ResolveStatus::Protected, "yt-dlp", "Provider unsupported: protected/DRM or separate inaccessible streams", {}};

    ResolveResult result{ResolveStatus::Resolved, "yt-dlp", {}, {title, {}}};
    Season season{"default", "Default", {}};
    VoiceTrack voice{"default", "Default", {}};
    voice.episodes.push_back({"default", title, std::move(streams)});
    season.voiceTracks.push_back(std::move(voice));
    result.entry.seasons.push_back(std::move(season));
    return result;
}

} // namespace wannaviewer
