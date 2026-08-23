#include "wannaviewer/resolvers/DirectMediaResolver.hpp"

namespace wannaviewer {

bool DirectMediaResolver::CanHandle(const Url& url) const { return url.IsDirectMedia(); }

ResolveResult DirectMediaResolver::Resolve(const Url& url, const ResolveContext&) const {
    std::string protocol = "http";
    if (url.Path().find(".m3u8") != std::string::npos) protocol = "hls";
    else if (url.Path().find(".mpd") != std::string::npos) protocol = "dash";
    ResolveResult result{ResolveStatus::Resolved, "direct-media", {}, {"Direct media", {}}};
    Season season{"default", "Default", {}};
    VoiceTrack voice{"default", "Default", {}};
    Episode episode{"default", "Media", {{url.Value(), {}, "Auto", {}, protocol, {}, false}}};
    voice.episodes.push_back(std::move(episode));
    season.voiceTracks.push_back(std::move(voice));
    result.entry.seasons.push_back(std::move(season));
    return result;
}

} // namespace wannaviewer
