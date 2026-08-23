#include "TestHarness.hpp"
#include "wannaviewer/resolvers/DirectMediaResolver.hpp"
#include "wannaviewer/resolvers/YummyAnimeResolver.hpp"
#include "wannaviewer/network/Process.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

WV_TEST("direct media resolver identifies HLS") {
    const auto url = wannaviewer::Url::Parse("https://media.example.test/master.m3u8");
    WV_REQUIRE(url.has_value());
    wannaviewer::DirectMediaResolver resolver;
    WV_REQUIRE(resolver.CanHandle(*url));
    wannaviewer::HttpClient http;
    wannaviewer::Logger logger;
    const auto result = resolver.Resolve(*url, {http, logger, {}});
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(result.entry.seasons.front().voiceTracks.front().episodes.front().streams.front().protocol == "hls");
}

WV_TEST("YummyAnime fixture normalizes seasons voices episodes and DRM") {
    const auto path = std::filesystem::path(WANNAVIEWER_SOURCE_DIR) / "tests" / "fixtures" / "yummyanime" / "page.html";
    std::ifstream input(path);
    std::ostringstream content;
    content << input.rdbuf();
    wannaviewer::YummyAnimeResolver resolver;
    const auto result = resolver.ParseFixture(content.str(), "https://yummyanime.tv/item");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(result.entry.title == "Fixture Anime");
    const auto& voice = result.entry.seasons.front().voiceTracks.front();
    WV_REQUIRE(voice.title == "Dub A");
    WV_REQUIRE(voice.episodes.front().streams.size() == 2);
    WV_REQUIRE(!voice.episodes.front().streams.front().protectedStream);
    WV_REQUIRE(voice.episodes.front().streams.back().protectedStream);
}

WV_TEST("yt-dlp helper process uses machine-readable pinned executable") {
    const auto executable = std::filesystem::path(WANNAVIEWER_SOURCE_DIR) / "tools" / "yt-dlp.exe";
    if (!std::filesystem::is_regular_file(executable)) return;
    const auto result = wannaviewer::RunProcess(executable, {"--version"}, std::chrono::seconds(10), 64U * 1024U);
    WV_REQUIRE(!result.timedOut);
    WV_REQUIRE(!result.cancelled);
    WV_REQUIRE(!result.outputTruncated);
    WV_REQUIRE(result.exitCode == 0);
    WV_REQUIRE(result.output.find("2026.08.19") != std::string::npos);
}

WV_TEST("YummyAnime live resolver is opt-in") {
#ifdef _WIN32
    char setting[8]{};
    if (GetEnvironmentVariableA("WANNAVIEWER_LIVE_RESOLVER_TESTS", setting, sizeof(setting)) != 1 || setting[0] != '1') return;
#else
    const char* enabled = std::getenv("WANNAVIEWER_LIVE_RESOLVER_TESTS");
    if (!enabled || std::string_view(enabled) != "1") return;
#endif
    const auto url = wannaviewer::Url::Parse("https://yummyanime.tv/8473-prekrasnaja-vechernjaja-luna-l4.html");
    WV_REQUIRE(url.has_value());
    wannaviewer::HttpClient http;
    wannaviewer::Logger logger;
    wannaviewer::YummyAnimeResolver resolver;
    const auto result = resolver.Resolve(*url, {http, logger, {}});
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    bool hasProvider = false;
    for (const auto& season : result.entry.seasons)
        for (const auto& voice : season.voiceTracks)
            for (const auto& episode : voice.episodes)
                for (const auto& stream : episode.streams)
                    if (stream.protocol == "embed") hasProvider = true;
    WV_REQUIRE(hasProvider);
}
