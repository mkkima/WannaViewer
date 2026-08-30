#include "TestHarness.hpp"
#include "wannaviewer/resolvers/AnimeGoResolver.hpp"
#include "wannaviewer/resolvers/AniBoomResolver.hpp"
#include "wannaviewer/resolvers/CdnVideoHubResolver.hpp"
#include "wannaviewer/resolvers/DirectMediaResolver.hpp"
#include "wannaviewer/resolvers/YummyAnimeResolver.hpp"
#include "wannaviewer/network/Process.hpp"
#ifdef _WIN32
#include "wannaviewer/playback/MpvEngine.hpp"
#include "wannaviewer/resolvers/BrowserEmbedResolver.hpp"
#endif

#include <atomic>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string ReadFixture(std::initializer_list<std::string_view> parts) {
    auto path = std::filesystem::path(WANNAVIEWER_SOURCE_DIR) / "tests" / "fixtures";
    for (const auto part : parts) path /= part;
    std::ifstream input(path, std::ios::binary);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}

std::size_t StreamCount(const wannaviewer::ResolveResult& result) {
    std::size_t count = 0;
    for (const auto& season : result.entry.seasons)
        for (const auto& voice : season.voiceTracks)
            for (const auto& episode : voice.episodes) count += episode.streams.size();
    return count;
}

const wannaviewer::StreamVariant* FirstStream(const wannaviewer::ResolveResult& result) {
    for (const auto& season : result.entry.seasons)
        for (const auto& voice : season.voiceTracks)
            for (const auto& episode : voice.episodes)
                if (!episode.streams.empty()) return &episode.streams.front();
    return nullptr;
}

const wannaviewer::StreamVariant* StreamForHost(const wannaviewer::ResolveResult& result,
                                                std::string_view host) {
    for (const auto& season : result.entry.seasons)
        for (const auto& voice : season.voiceTracks)
            for (const auto& episode : voice.episodes)
                for (const auto& stream : episode.streams) {
                    const auto url = wannaviewer::Url::Parse(stream.url);
                    if (url && url->HostIs(host)) return &stream;
                }
    return nullptr;
}

bool LiveResolverTestsEnabled() {
#ifdef _WIN32
    char setting[8]{};
    return GetEnvironmentVariableA("WANNAVIEWER_LIVE_RESOLVER_TESTS", setting, sizeof(setting)) == 1 &&
           setting[0] == '1';
#else
    const char* enabled = std::getenv("WANNAVIEWER_LIVE_RESOLVER_TESTS");
    return enabled && std::string_view(enabled) == "1";
#endif
}

#ifdef _WIN32
void RequireLiveVideoPlayback(const wannaviewer::StreamVariant& stream) {
    const auto discovered = wannaviewer::AppPaths::Discover();
    auto paths = discovered;
    paths.root = discovered.root / "bin";
    paths.cache = std::filesystem::temp_directory_path() / "wannaviewer-live-mpv-cache";
    std::filesystem::create_directories(paths.cache);

    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"WannaViewer live playback test",
                                        WS_POPUP, 0, 0, 640, 360, nullptr, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
    if (!window) throw std::runtime_error("Unable to create the live playback test window");

    std::atomic_bool playbackStarted{false};
    std::atomic_bool videoConfigured{false};
    std::atomic_bool playbackFailed{false};
    std::mutex errorMutex;
    std::string playbackError;
    wannaviewer::Config config;
    wannaviewer::Logger logger;
    logger.Open(paths.cache / "logs", wannaviewer::LogLevel::Debug);
    try {
        wannaviewer::MpvEngine engine(paths, config, logger);
        engine.Initialize(reinterpret_cast<std::uintptr_t>(window), [&](wannaviewer::PlaybackEvent event) {
            if (event.type == wannaviewer::PlaybackEventType::PlaybackStarted) playbackStarted = true;
            else if (event.type == wannaviewer::PlaybackEventType::VideoReconfigured) videoConfigured = true;
            else if (event.type == wannaviewer::PlaybackEventType::Error) {
                std::scoped_lock lock(errorMutex);
                playbackError = event.name + ": " + event.value;
                playbackFailed = true;
            }
        });
        std::vector<std::pair<std::string, std::string>> headers(stream.headers.begin(), stream.headers.end());
        engine.Open(stream.url, headers, stream.audioUrl);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(35);
        while ((!playbackStarted || !videoConfigured) && !playbackFailed &&
               std::chrono::steady_clock::now() < deadline) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        engine.Shutdown();
    } catch (...) {
        DestroyWindow(window);
        throw;
    }
    DestroyWindow(window);
    if (playbackFailed) {
        std::scoped_lock lock(errorMutex);
        throw std::runtime_error("libmpv failed before the first moving video frame: " + playbackError);
    }
    if (!videoConfigured || !playbackStarted)
        throw std::runtime_error("libmpv did not decode video and advance playback time within 35 seconds");
}
#endif

} // namespace

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
    wannaviewer::YummyAnimeResolver resolver;
    const auto result = resolver.ParseFixture(ReadFixture({"yummyanime", "page.html"}), "https://yummyanime.tv/item");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(result.entry.title == "Fixture Anime");
    const auto& voice = result.entry.seasons.front().voiceTracks.front();
    WV_REQUIRE(voice.title == "Dub A");
    WV_REQUIRE(voice.episodes.front().streams.size() == 2);
    WV_REQUIRE(!voice.episodes.front().streams.front().protectedStream);
    WV_REQUIRE(voice.episodes.front().streams.back().protectedStream);
}

WV_TEST("YummyAnime public API exposes CVH Kodik and Alloha provider chains") {
    wannaviewer::YummyAnimeResolver resolver;
    const auto result = resolver.ParseApiFixture(ReadFixture({"yummyanime", "api.json"}),
                                                 "https://ru.yummyani.me/catalog/item/fixture");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(result.entry.title == "Fixture Yummy Anime");
    WV_REQUIRE(result.entry.seasons.size() == 1);
    WV_REQUIRE(result.entry.seasons.front().voiceTracks.size() == 2);
    WV_REQUIRE(StreamCount(result) == 5);
    bool hasCvh = false;
    bool hasKodik = false;
    bool hasAlloha = false;
    for (const auto& voice : result.entry.seasons.front().voiceTracks)
        for (const auto& episode : voice.episodes)
            for (const auto& stream : episode.streams) {
                WV_REQUIRE(stream.protocol == "embed");
                hasCvh = hasCvh || stream.url.find("/iframeCVH.html") != std::string::npos;
                hasKodik = hasKodik || stream.url.find("kodikplayer.com") != std::string::npos;
                hasAlloha = hasAlloha || stream.url.find("alloha.yani.tv") != std::string::npos;
            }
    WV_REQUIRE(hasCvh);
    WV_REQUIRE(hasKodik);
    WV_REQUIRE(hasAlloha);
}

WV_TEST("AnimeGo catalog exposes episode endpoints without prefetching every provider") {
    wannaviewer::AnimeGoResolver resolver;
    const auto result = resolver.ParsePageFixture(ReadFixture({"animego", "page.html"}),
                                                  ReadFixture({"animego", "player.json"}),
                                                  "https://animego.me/anime/fixture-2855");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(result.entry.title == "Fixture AnimeGo");
    const auto& episodes = result.entry.seasons.front().voiceTracks.front().episodes;
    WV_REQUIRE(episodes.size() == 2);
    WV_REQUIRE(episodes.front().streams.front().url == "https://animego.me/player/videos/37572");
}

WV_TEST("AnimeGo catalog accepts current carousel episode markup") {
    wannaviewer::AnimeGoResolver resolver;
    const auto result = resolver.ParsePageFixture(ReadFixture({"animego", "page.html"}),
                                                  ReadFixture({"animego", "player-modern.json"}),
                                                  "https://animego.me/anime/fixture-2855");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    const auto& episodes = result.entry.seasons.front().voiceTracks.front().episodes;
    WV_REQUIRE(episodes.size() == 2);
    WV_REQUIRE(episodes.front().title == "Episode 1");
    WV_REQUIRE(episodes.front().streams.front().url == "https://animego.me/player/videos/37572");
}

WV_TEST("AnimeGo episode exposes CVH AniBoom and Kodik") {
    wannaviewer::AnimeGoResolver resolver;
    const auto result = resolver.ParseEpisodeFixture(ReadFixture({"animego", "episode.json"}),
                                                     "https://animego.me/anime/fixture-2855");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(StreamCount(result) == 3);
}

#ifdef _WIN32
WV_TEST("browser embed resolver accepts only supported HTTPS provider hosts") {
    wannaviewer::BrowserEmbedResolver resolver(std::filesystem::temp_directory_path() / "wannaviewer-webview2-test");
    const auto kodik = wannaviewer::Url::Parse("https://kodikplayer.com/seria/1/hash/720p");
    const auto alloha = wannaviewer::Url::Parse("https://alloha.yani.tv/?token=fixture");
    const auto insecure = wannaviewer::Url::Parse("http://kodikplayer.com/seria/1/hash/720p");
    const auto unrelated = wannaviewer::Url::Parse("https://example.test/video");
    WV_REQUIRE(kodik.has_value());
    WV_REQUIRE(alloha.has_value());
    WV_REQUIRE(insecure.has_value());
    WV_REQUIRE(unrelated.has_value());
    WV_REQUIRE(resolver.CanHandle(*kodik));
    WV_REQUIRE(resolver.CanHandle(*alloha));
    WV_REQUIRE(!resolver.CanHandle(*insecure));
    WV_REQUIRE(!resolver.CanHandle(*unrelated));
}
#endif

WV_TEST("AniBoom fixture extracts the public HLS manifest from escaped player metadata") {
    wannaviewer::AniBoomResolver resolver;
    const auto result = resolver.ParseFixture(ReadFixture({"aniboom", "embed.html"}),
                                              "https://aniboom.one/embed/fixture");
    WV_REQUIRE(result.status == wannaviewer::ResolveStatus::Resolved);
    const auto& stream = result.entry.seasons.front().voiceTracks.front().episodes.front().streams.front();
    WV_REQUIRE(stream.protocol == "hls");
    WV_REQUIRE(stream.url == "https://media.example.test/master.m3u8");
}

WV_TEST("CVH fixtures filter the selected episode and voice then expose HLS") {
    wannaviewer::CdnVideoHubResolver resolver;
    const auto playlist = resolver.ParsePlaylistFixture(ReadFixture({"cdnvideohub", "playlist.json"}),
                                                        "https://ru.yummyani.me/iframeCVH.html?anime_id=123",
                                                        "1", "AniLiberty");
    WV_REQUIRE(playlist.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(StreamCount(playlist) == 1);
    const auto& endpoint = playlist.entry.seasons.front().voiceTracks.front().episodes.front().streams.front();
    WV_REQUIRE(endpoint.url == "https://plapi.cdnvideohub.com/api/v1/player/sv/video/12919642479344");
    WV_REQUIRE(endpoint.protocol == "embed");
    WV_REQUIRE(!endpoint.headers.contains("Origin"));

    auto inheritedHeaders = endpoint.headers;
    inheritedHeaders.emplace("Origin", "https://ru.yummyani.me");
    const auto source = resolver.ParseSourceFixture(ReadFixture({"cdnvideohub", "source.json"}), endpoint.url,
                                                    std::move(inheritedHeaders));
    WV_REQUIRE(source.status == wannaviewer::ResolveStatus::Resolved);
    const auto& stream = source.entry.seasons.front().voiceTracks.front().episodes.front().streams.front();
    WV_REQUIRE(stream.protocol == "hls");
    WV_REQUIRE(stream.url.find("video.m3u8") != std::string::npos);
    WV_REQUIRE(!stream.headers.contains("Origin"));
}

WV_TEST("yt-dlp helper process uses machine-readable pinned executable") {
    const auto executable = std::filesystem::path(WANNAVIEWER_SOURCE_DIR) / "tools" / "yt-dlp.exe";
    if (!std::filesystem::is_regular_file(executable)) return;
    // First launch can be delayed by Windows attachment scanning on clean
    // machines. This test verifies the pinned helper, not cold-start latency;
    // the signed 37 MB executable has exceeded 30 seconds on a cold cache.
    const auto result = wannaviewer::RunProcess(executable, {"--version"}, std::chrono::seconds(60), 64U * 1024U);
    WV_REQUIRE(!result.timedOut);
    WV_REQUIRE(!result.cancelled);
    WV_REQUIRE(!result.outputTruncated);
    WV_REQUIRE(result.exitCode == 0);
    WV_REQUIRE(result.output.find("2026.08.19") != std::string::npos);
}

WV_TEST("YummyAnime live resolver is opt-in") {
    if (!LiveResolverTestsEnabled()) return;
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

WV_TEST("supplied YummyAnime URL resolves through CVH to public HLS when live tests are enabled") {
    if (!LiveResolverTestsEnabled()) return;
    const auto url = wannaviewer::Url::Parse("https://ru.yummyani.me/catalog/item/angel-po-sosedstvu-2");
    WV_REQUIRE(url.has_value());
    wannaviewer::HttpClient http;
    wannaviewer::Logger logger;
    wannaviewer::YummyAnimeResolver pageResolver;
    wannaviewer::CdnVideoHubResolver cvhResolver;
    const auto catalog = pageResolver.Resolve(*url, {http, logger, {}});
    WV_REQUIRE(catalog.status == wannaviewer::ResolveStatus::Resolved);
    const auto* embed = StreamForHost(catalog, "yummyani.me");
    WV_REQUIRE(embed != nullptr);
    const auto embedUrl = wannaviewer::Url::Parse(embed->url);
    WV_REQUIRE(embedUrl.has_value());
    WV_REQUIRE(cvhResolver.CanHandle(*embedUrl));
    const auto playlist = cvhResolver.Resolve(*embedUrl, {http, logger, {}, embed->headers});
    WV_REQUIRE(playlist.status == wannaviewer::ResolveStatus::Resolved);
    const auto* sourceEndpoint = FirstStream(playlist);
    WV_REQUIRE(sourceEndpoint != nullptr);
    const auto sourceUrl = wannaviewer::Url::Parse(sourceEndpoint->url);
    WV_REQUIRE(sourceUrl.has_value());
    const auto source = cvhResolver.Resolve(*sourceUrl, {http, logger, {}, sourceEndpoint->headers});
    WV_REQUIRE(source.status == wannaviewer::ResolveStatus::Resolved);
    const auto* hls = FirstStream(source);
    WV_REQUIRE(hls != nullptr);
    WV_REQUIRE(hls->protocol == "hls");
#ifdef _WIN32
    RequireLiveVideoPlayback(*hls);
#endif
}

#ifdef _WIN32
WV_TEST("supplied YummyAnime URL resolves Alloha and Kodik in WebView2 when live tests are enabled") {
    if (!LiveResolverTestsEnabled()) return;
    const auto url = wannaviewer::Url::Parse("https://ru.yummyani.me/catalog/item/angel-po-sosedstvu-2");
    WV_REQUIRE(url.has_value());
    wannaviewer::HttpClient http;
    wannaviewer::Logger logger;
    logger.Open(std::filesystem::temp_directory_path() / "wannaviewer-browser-resolver-logs",
                wannaviewer::LogLevel::Debug);
    wannaviewer::YummyAnimeResolver pageResolver;
    const auto catalog = pageResolver.Resolve(*url, {http, logger, {}});
    WV_REQUIRE(catalog.status == wannaviewer::ResolveStatus::Resolved);

    const std::array<std::string_view, 2> providerHosts{"alloha.yani.tv", "kodikplayer.com"};
    for (const auto providerHost : providerHosts) {
        const auto* embed = StreamForHost(catalog, providerHost);
        WV_REQUIRE(embed != nullptr);
        const auto embedUrl = wannaviewer::Url::Parse(embed->url);
        WV_REQUIRE(embedUrl.has_value());
        const auto profile = std::filesystem::temp_directory_path() /
                             ("wannaviewer-webview2-" + std::string(providerHost));
        wannaviewer::BrowserEmbedResolver browserResolver(profile);
        bool played = false;
        std::string lastError;
        for (unsigned attempt = 0; attempt < 2 && !played; ++attempt) {
            const auto source = browserResolver.Resolve(*embedUrl, {http, logger, {}, embed->headers});
            if (source.status != wannaviewer::ResolveStatus::Resolved) {
                lastError = source.message;
                continue;
            }
            const auto* media = FirstStream(source);
            if (!media || (media->protocol != "hls" && media->protocol != "dash" && media->protocol != "http") ||
                !wannaviewer::Url::Parse(media->url)) {
                lastError = "resolver returned invalid media";
                continue;
            }
            // Do not preflight the captured manifest here. Some short-lived provider
            // URLs rate-limit duplicate immediate reads; libmpv playback below is the
            // actual contract and is a stronger check than a second HTTP GET.
            try {
                RequireLiveVideoPlayback(*media);
                played = true;
            } catch (const std::exception& error) {
                lastError = error.what();
            }
        }
        if (!played) throw std::runtime_error(std::string(providerHost) + " after one retry: " + lastError);
    }
}
#endif

WV_TEST("supplied AnimeGo URL resolves episodes and supported providers when live tests are enabled") {
    if (!LiveResolverTestsEnabled()) return;
    const auto url = wannaviewer::Url::Parse("https://animego.me/anime/dlya-tebya-bessmertnyy-3-2855");
    WV_REQUIRE(url.has_value());
    wannaviewer::HttpClient http;
    wannaviewer::Logger logger;
    wannaviewer::AnimeGoResolver pageResolver;
    const auto catalog = pageResolver.Resolve(*url, {http, logger, {}});
    WV_REQUIRE(catalog.status == wannaviewer::ResolveStatus::Resolved);
    const auto* episode = FirstStream(catalog);
    WV_REQUIRE(episode != nullptr);
    const auto episodeUrl = wannaviewer::Url::Parse(episode->url);
    WV_REQUIRE(episodeUrl.has_value());
    const auto providers = pageResolver.Resolve(*episodeUrl, {http, logger, {}, episode->headers});
    WV_REQUIRE(providers.status == wannaviewer::ResolveStatus::Resolved);
    WV_REQUIRE(StreamCount(providers) > 0);
    WV_REQUIRE(StreamForHost(providers, "kodikplayer.com") != nullptr);
    const auto* provider = FirstStream(providers);
    WV_REQUIRE(provider != nullptr);
    const auto providerUrl = wannaviewer::Url::Parse(provider->url);
    WV_REQUIRE(providerUrl.has_value());
    wannaviewer::CdnVideoHubResolver cvhResolver;
    wannaviewer::AniBoomResolver aniBoomResolver;
    if (cvhResolver.CanHandle(*providerUrl)) {
        const auto playlist = cvhResolver.Resolve(*providerUrl, {http, logger, {}, provider->headers});
        WV_REQUIRE(playlist.status == wannaviewer::ResolveStatus::Resolved);
        const auto* sourceEndpoint = FirstStream(playlist);
        WV_REQUIRE(sourceEndpoint != nullptr);
        const auto sourceUrl = wannaviewer::Url::Parse(sourceEndpoint->url);
        WV_REQUIRE(sourceUrl.has_value());
        const auto source = cvhResolver.Resolve(*sourceUrl, {http, logger, {}, sourceEndpoint->headers});
        WV_REQUIRE(source.status == wannaviewer::ResolveStatus::Resolved);
        const auto* media = FirstStream(source);
        WV_REQUIRE(media != nullptr);
#ifdef _WIN32
        RequireLiveVideoPlayback(*media);
#endif
    } else {
        WV_REQUIRE(aniBoomResolver.CanHandle(*providerUrl));
        const auto source = aniBoomResolver.Resolve(*providerUrl, {http, logger, {}, provider->headers});
        WV_REQUIRE(source.status == wannaviewer::ResolveStatus::Resolved);
        WV_REQUIRE(FirstStream(source) != nullptr);
    }
}
