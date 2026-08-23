#include "TestHarness.hpp"
#include "wannaviewer/core/Config.hpp"

#include <filesystem>
#include <fstream>

WV_TEST("config parses bounded typed values") {
    const auto path = std::filesystem::temp_directory_path() / "wannaviewer-config-test.conf";
    {
        std::ofstream output(path);
        output << "feature = yes\nnumber=999\ntext = stable\ninvalid=nope\n";
    }
    auto config = wannaviewer::Config::Load(path);
    WV_REQUIRE(config.GetBool("feature", false));
    WV_REQUIRE(config.GetInt("number", 1, 0, 100) == 100);
    WV_REQUIRE(config.GetInt("invalid", 7, 0, 100) == 7);
    WV_REQUIRE(config.GetString("text") == "stable");
    std::error_code error;
    std::filesystem::remove(path, error);
}

WV_TEST("config saves atomically readable output") {
    const auto path = std::filesystem::temp_directory_path() / "wannaviewer-config-save-test.conf";
    wannaviewer::Config config;
    config.Set("z", "last");
    config.Set("a", "first");
    config.Save(path);
    auto loaded = wannaviewer::Config::Load(path);
    WV_REQUIRE(loaded.GetString("a") == "first");
    WV_REQUIRE(loaded.GetString("z") == "last");
    std::error_code error;
    std::filesystem::remove(path, error);
}
