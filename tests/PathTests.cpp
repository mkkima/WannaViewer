#include "TestHarness.hpp"
#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/core/Path.hpp"
#include "wannaviewer/rendering/ShaderManager.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <string>

WV_TEST("filesystem paths convert to UTF-8 independently of the Windows code page") {
    const std::u8string expected = u8"C:\\Видео\\日本語\\😀.mkv";
#ifdef _WIN32
    const std::filesystem::path path(L"C:\\Видео\\日本語\\😀.mkv");
#else
    const std::filesystem::path path(expected);
#endif
    const std::string expectedBytes(reinterpret_cast<const char*>(expected.data()), expected.size());
    WV_REQUIRE(wannaviewer::PathToUtf8(path) == expectedBytes);
}

WV_TEST("logger opens and rotates files under a Unicode directory") {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const auto directory = std::filesystem::temp_directory_path() /
                           std::filesystem::path(L"wannaviewer-日本語-😀") / std::to_wstring(nonce);
#else
    const auto directory = std::filesystem::temp_directory_path() /
                           std::filesystem::path(u8"wannaviewer-日本語-😀") / std::to_string(nonce);
#endif
    {
        wannaviewer::Logger logger;
        logger.Open(directory, wannaviewer::LogLevel::Trace, 128, 2);
        for (int index = 0; index < 4; ++index) {
            logger.Write(wannaviewer::LogLevel::Error, "unicode-test", std::string(160, 'x'));
        }
    }
    const auto current = directory / "wannaviewer.log";
    auto rotated = current;
#ifdef _WIN32
    rotated += L".1";
#else
    rotated += ".1";
#endif
    WV_REQUIRE(std::filesystem::is_regular_file(current));
    WV_REQUIRE(std::filesystem::is_regular_file(rotated));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    error.clear();
    std::filesystem::remove(directory.parent_path(), error);
}

WV_TEST("missing resources report their Unicode path without a code-page failure") {
#ifdef _WIN32
    const std::filesystem::path root(L"C:\\WannaViewer-日本語-😀");
#else
    const std::filesystem::path root(u8"/tmp/WannaViewer-日本語-😀");
#endif
    const auto preset = root / "presets" / "missing-shaders.json";
    wannaviewer::ShaderManager manager(root / "shaders", preset);
    std::string message;
    try {
        manager.Reload();
    } catch (const std::exception& error) {
        message = error.what();
    }
    WV_REQUIRE(message.find("Shader preset file is missing") != std::string::npos);
    WV_REQUIRE(message.find(wannaviewer::PathToUtf8(preset)) != std::string::npos);
}
