#include "TestHarness.hpp"
#include "wannaviewer/playback/RecentMediaStore.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::filesystem::path TemporaryRecentPath(std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("wannaviewer-recent-media-" + std::to_string(nonce) + '-' + std::string(suffix) + ".json");
}

std::string RecentKey(char value) {
    return std::string(64, value);
}

wannaviewer::RecentMediaEntry LocalEntry(char key, std::string path) {
    return {RecentKey(key), wannaviewer::RecentMediaType::LocalFile, std::move(path),
            "Movie.mp4", "D:\\Videos", RecentKey(key), std::nullopt, 0};
}

} // namespace

WV_TEST("recent media round-trips page selections and promotes duplicates") {
    const auto path = TemporaryRecentPath("roundtrip");
    wannaviewer::RecentMediaStore store;
    WV_REQUIRE(store.Upsert(LocalEntry('a', "D:\\Videos\\Movie.mp4")));
    wannaviewer::RecentMediaEntry page{
        RecentKey('b'), wannaviewer::RecentMediaType::WebPage, "https://example.test/show", "Example show",
        "Season 2 · Voice · Episode 4", RecentKey('c'),
        wannaviewer::RecentMediaSelection{"s2", "Season 2", "v1", "Voice", "e4", "Episode 4", "1080p", "hls"}, 0};
    WV_REQUIRE(store.Upsert(page));
    WV_REQUIRE(store.Upsert(LocalEntry('a', "D:\\Videos\\Movie.mp4")));
    WV_REQUIRE(store.Size() == 2);
    WV_REQUIRE(store.Entries().front().id == RecentKey('a'));
    store.Save(path);

    const auto loaded = wannaviewer::RecentMediaStore::Load(path);
    WV_REQUIRE(loaded.Size() == 2);
    const auto* restored = loaded.Find(RecentKey('b'));
    WV_REQUIRE(restored != nullptr);
    WV_REQUIRE(restored->type == wannaviewer::RecentMediaType::WebPage);
    WV_REQUIRE(restored->selection.has_value());
    WV_REQUIRE(restored->selection->episodeId == "e4");
    WV_REQUIRE(restored->selection->quality == "1080p");
    auto mutableStore = loaded;
    WV_REQUIRE(mutableStore.Remove(RecentKey('a')));
    WV_REQUIRE(!mutableStore.Remove(RecentKey('a')));
    WV_REQUIRE(mutableStore.Clear());
    WV_REQUIRE(!mutableStore.Clear());

    std::error_code error;
    std::filesystem::remove(path, error);
}

WV_TEST("recent media stays bounded and ignores malformed entries") {
    const auto path = TemporaryRecentPath("malformed");
    {
        std::ofstream output(path);
        output << "{\"schema_version\":1,\"entries\":["
               << "{\"id\":\"bad\",\"type\":\"file\",\"open_value\":\"x\",\"title\":\"x\",\"updated_unix_seconds\":1},"
               << "{\"id\":\"" << RecentKey('a') << "\",\"type\":\"file\",\"open_value\":\"x\",\"title\":\"x\",\"updated_unix_seconds\":2}"
               << "]}";
    }
    auto store = wannaviewer::RecentMediaStore::Load(path);
    WV_REQUIRE(store.Size() == 1);
    for (std::size_t index = 0; index < wannaviewer::RecentMediaStore::MaximumEntries + 5; ++index) {
        constexpr char digits[] = "0123456789abcdef";
        std::string key(64, '0');
        key[62] = digits[(index / 16) % 16];
        key[63] = digits[index % 16];
        auto entry = LocalEntry('b', "D:\\Videos\\" + std::to_string(index) + ".mp4");
        entry.id = std::move(key);
        WV_REQUIRE(store.Upsert(std::move(entry)));
    }
    WV_REQUIRE(store.Size() == wannaviewer::RecentMediaStore::MaximumEntries);

    std::error_code error;
    std::filesystem::remove(path, error);
}

WV_TEST("recent media preserves unsupported files") {
    const auto path = TemporaryRecentPath("unsupported");
    const std::string original = R"({"schema_version":99,"entries":[]})";
    {
        std::ofstream output(path);
        output << original;
    }
    bool rejected = false;
    try {
        (void)wannaviewer::RecentMediaStore::Load(path);
    } catch (const std::exception&) {
        rejected = true;
    }
    WV_REQUIRE(rejected);
    std::ifstream input(path);
    const std::string after((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    WV_REQUIRE(after == original);

    std::error_code error;
    std::filesystem::remove(path, error);
}

WV_TEST("recent media matches a refreshed episode without reusing its old stream URL") {
    wannaviewer::AnimeEntry entry{
        "Example show",
        {{"new-season-id", "Season 2", {{"new-voice-id", "Voice", {{"new-episode-id", "Episode 4", {
            {"https://cdn.test/old-choice.m3u8", {}, "720p", "h264", "hls", {}, false},
            {"https://cdn.test/current.m3u8", {}, "1080p", "h264", "hls", {}, false},
            {"https://cdn.test/protected.mpd", {}, "1080p", "av1", "dash", {}, true}
        }}}}}}}
    };
    const wannaviewer::RecentMediaSelection selection{
        "old-season-id", "Season 2", "old-voice-id", "Voice", "old-episode-id", "Episode 4", "1080p", "hls"};
    const auto match = wannaviewer::MatchRecentMediaSelection(entry, selection);
    WV_REQUIRE(match.has_value());
    WV_REQUIRE(match->season == 0 && match->voice == 0 && match->episode == 0 && match->stream == 1);
    WV_REQUIRE(entry.seasons[match->season].voiceTracks[match->voice].episodes[match->episode]
                   .streams[match->stream].url == "https://cdn.test/current.m3u8");

    auto unavailable = selection;
    unavailable.episodeId = "missing";
    unavailable.episodeTitle = "Missing episode";
    WV_REQUIRE(!wannaviewer::MatchRecentMediaSelection(entry, unavailable).has_value());
}
