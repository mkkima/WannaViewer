#include "TestHarness.hpp"
#include "wannaviewer/playback/PlaybackStateStore.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace {

std::filesystem::path TemporaryStatePath(std::string_view suffix) {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("wannaviewer-playback-state-" + std::to_string(nonce) + '-' + std::string(suffix) + ".json");
}

std::string StateKey(char value) {
    return std::string(64, value);
}

} // namespace

WV_TEST("playback state round-trips resumable positions") {
    const auto path = TemporaryStatePath("roundtrip");
    wannaviewer::PlaybackStateStore state;
    WV_REQUIRE(state.Update(StateKey('a'), 321.5, 1200.0));
    state.Save(path);
    WV_REQUIRE(state.Update(StateKey('a'), 322.5, 1200.0));
    state.Save(path);

    const auto loaded = wannaviewer::PlaybackStateStore::Load(path);
    const auto progress = loaded.Find(StateKey('a'));
    WV_REQUIRE(progress.has_value());
    WV_REQUIRE(progress->positionSeconds == 322.5);
    WV_REQUIRE(progress->durationSeconds == 1200.0);
    WV_REQUIRE(loaded.ResumePosition(StateKey('a'), 1200.0) == std::optional<double>(322.5));
    auto cleared = loaded;
    WV_REQUIRE(cleared.Clear());
    WV_REQUIRE(cleared.Size() == 0);
    WV_REQUIRE(!cleared.Clear());

    std::error_code error;
    std::filesystem::remove(path, error);
}

WV_TEST("playback state rejects unsafe resume boundaries and changed media") {
    wannaviewer::PlaybackStateStore state;
    WV_REQUIRE(!state.Update(StateKey('a'), 5.0, 1200.0));
    WV_REQUIRE(!state.Update(StateKey('b'), 45.0, 50.0));
    WV_REQUIRE(!state.Update(StateKey('c'), 1180.0, 1200.0));
    WV_REQUIRE(state.Update(StateKey('d'), 400.0, 1200.0));
    WV_REQUIRE(!state.ResumePosition(StateKey('d'), 900.0).has_value());
    WV_REQUIRE(state.ResumePosition(StateKey('d'), 1198.0).has_value());
}

WV_TEST("playback state stays bounded and ignores malformed entries") {
    const auto path = TemporaryStatePath("bounded");
    {
        std::ofstream output(path);
        output << "{\"schema_version\":1,\"entries\":{"
               << "\"invalid\":{\"position_seconds\":20,\"duration_seconds\":100,\"updated_unix_seconds\":1},"
               << "\"" << StateKey('a') << "\":{\"position_seconds\":\"wrong type\",\"duration_seconds\":100,\"updated_unix_seconds\":1},"
               << "\"" << StateKey('b') << "\":{\"position_seconds\":20,\"duration_seconds\":100,\"updated_unix_seconds\":1}"
               << "}}";
    }
    auto state = wannaviewer::PlaybackStateStore::Load(path);
    WV_REQUIRE(state.Size() == 1);
    WV_REQUIRE(state.Find(StateKey('b')).has_value());
    for (std::size_t index = 0; index < wannaviewer::PlaybackStateStore::MaximumEntries + 10; ++index) {
        std::string key(64, '0');
        constexpr char digits[] = "0123456789abcdef";
        key[62] = digits[(index / 16) % 16];
        key[63] = digits[index % 16];
        WV_REQUIRE(state.Update(std::move(key), 60.0 + static_cast<double>(index), 600.0));
    }
    WV_REQUIRE(state.Size() == wannaviewer::PlaybackStateStore::MaximumEntries);

    std::error_code error;
    std::filesystem::remove(path, error);
}

WV_TEST("playback state preserves an unsupported file instead of overwriting it") {
    const auto path = TemporaryStatePath("unsupported");
    const std::string original = R"({"schema_version":99,"entries":{}})";
    {
        std::ofstream output(path);
        output << original;
    }
    bool rejected = false;
    try {
        (void)wannaviewer::PlaybackStateStore::Load(path);
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
