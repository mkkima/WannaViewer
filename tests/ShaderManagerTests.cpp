#include "TestHarness.hpp"
#include "wannaviewer/rendering/ShaderManager.hpp"

#include <filesystem>
#include <fstream>

WV_TEST("shader manager resolves only files inside its root") {
    const auto base = std::filesystem::temp_directory_path() / "wannaviewer-shader-test";
    const auto root = base / "shaders";
    std::filesystem::create_directories(root / "Anime4K");
    { std::ofstream shader(root / "Anime4K" / "safe.glsl"); shader << "//!HOOK MAIN\n"; }
    const auto presets = base / "presets.json";
    {
        std::ofstream output(presets);
        output << R"({"presets":[{"id":"off","name":"Off","hotkey":0,"shaders":[]},{"id":"safe","name":"Safe","hotkey":1,"shaders":["Anime4K/safe.glsl"]}]})";
    }
    wannaviewer::ShaderManager manager(root, presets);
    manager.Reload();
    WV_REQUIRE(manager.Presets().size() == 2);
    WV_REQUIRE(manager.Resolve(*manager.ForHotkey(1)).front().filename() == "safe.glsl");
    std::error_code error;
    std::filesystem::remove_all(base, error);
}
