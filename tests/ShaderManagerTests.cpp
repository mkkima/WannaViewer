#include "TestHarness.hpp"
#include "wannaviewer/rendering/ShaderManager.hpp"

#include <array>
#include <filesystem>
#include <fstream>

namespace {

std::vector<std::string> ShaderFilenames(const wannaviewer::ShaderManager& manager,
                                         std::string_view id) {
    const auto* preset = manager.Find(id);
    if (!preset) return {};
    std::vector<std::string> result;
    for (const auto& path : manager.Resolve(*preset)) result.push_back(path.filename().string());
    return result;
}

} // namespace

WV_TEST("shader manager resolves only files inside its root") {
    const auto base = std::filesystem::temp_directory_path() / "wannaviewer-shader-test";
    const auto root = base / "shaders";
    std::filesystem::create_directories(root / "Anime4K");
    { std::ofstream shader(root / "Anime4K" / "safe.glsl"); shader << "//!HOOK MAIN\n"; }
    const auto presets = base / "presets.json";
    {
        std::ofstream output(presets);
        output << R"({"presets":[{"id":"off","name":"Off","hotkey":0,"shaders":[]},{"id":"safe","name":"Safe","aliases":["legacy-safe"],"hotkey":1,"shaders":["Anime4K/safe.glsl"]}]})";
    }
    wannaviewer::ShaderManager manager(root, presets);
    manager.Reload();
    WV_REQUIRE(manager.Presets().size() == 2);
    WV_REQUIRE(manager.Resolve(*manager.ForHotkey(1)).front().filename() == "safe.glsl");
    WV_REQUIRE(manager.Find("legacy-safe") == manager.Find("safe"));
    std::error_code error;
    std::filesystem::remove_all(base, error);
}

WV_TEST("bundled Anime4K presets match the official Fast processing modes") {
    const std::filesystem::path sourceRoot(WANNAVIEWER_SOURCE_DIR);
    wannaviewer::ShaderManager manager(sourceRoot / "shaders",
                                       sourceRoot / "presets" / "shaders.json");
    manager.Reload();

    const std::array<std::string_view, 7> ids{
        "off", "anime4k-a", "anime4k-b", "anime4k-c", "anime4k-a+a", "anime4k-b+b", "anime4k-c+a"};
    const std::array<std::string_view, 7> names{
        "Anime4K Off", "Anime4K A", "Anime4K B", "Anime4K C",
        "Anime4K A+A", "Anime4K B+B", "Anime4K C+A"};
    WV_REQUIRE(manager.Presets().size() == ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        WV_REQUIRE(manager.Presets()[index].id == ids[index]);
        WV_REQUIRE(manager.Presets()[index].name == names[index]);
        WV_REQUIRE(manager.ForHotkey(static_cast<int>(index)) == &manager.Presets()[index]);
    }

    const std::vector<std::string> modeA{
        "Anime4K_Clamp_Highlights.glsl", "Anime4K_Restore_CNN_M.glsl",
        "Anime4K_Upscale_CNN_x2_M.glsl", "Anime4K_AutoDownscalePre_x2.glsl",
        "Anime4K_AutoDownscalePre_x4.glsl", "Anime4K_Upscale_CNN_x2_S.glsl"};
    const std::vector<std::string> modeB{
        "Anime4K_Clamp_Highlights.glsl", "Anime4K_Restore_CNN_Soft_M.glsl",
        "Anime4K_Upscale_CNN_x2_M.glsl", "Anime4K_AutoDownscalePre_x2.glsl",
        "Anime4K_AutoDownscalePre_x4.glsl", "Anime4K_Upscale_CNN_x2_S.glsl"};
    const std::vector<std::string> modeC{
        "Anime4K_Clamp_Highlights.glsl", "Anime4K_Upscale_Denoise_CNN_x2_M.glsl",
        "Anime4K_AutoDownscalePre_x2.glsl", "Anime4K_AutoDownscalePre_x4.glsl",
        "Anime4K_Upscale_CNN_x2_S.glsl"};
    const std::vector<std::string> modeAA{
        "Anime4K_Clamp_Highlights.glsl", "Anime4K_Restore_CNN_M.glsl",
        "Anime4K_Upscale_CNN_x2_M.glsl", "Anime4K_Restore_CNN_S.glsl",
        "Anime4K_AutoDownscalePre_x2.glsl", "Anime4K_AutoDownscalePre_x4.glsl",
        "Anime4K_Upscale_CNN_x2_S.glsl"};
    const std::vector<std::string> modeBB{
        "Anime4K_Clamp_Highlights.glsl", "Anime4K_Restore_CNN_Soft_M.glsl",
        "Anime4K_Upscale_CNN_x2_M.glsl", "Anime4K_AutoDownscalePre_x2.glsl",
        "Anime4K_AutoDownscalePre_x4.glsl", "Anime4K_Restore_CNN_Soft_S.glsl",
        "Anime4K_Upscale_CNN_x2_S.glsl"};
    const std::vector<std::string> modeCA{
        "Anime4K_Clamp_Highlights.glsl", "Anime4K_Upscale_Denoise_CNN_x2_M.glsl",
        "Anime4K_AutoDownscalePre_x2.glsl", "Anime4K_AutoDownscalePre_x4.glsl",
        "Anime4K_Restore_CNN_S.glsl", "Anime4K_Upscale_CNN_x2_S.glsl"};

    WV_REQUIRE(ShaderFilenames(manager, "anime4k-a") == modeA);
    WV_REQUIRE(ShaderFilenames(manager, "anime4k-b") == modeB);
    WV_REQUIRE(ShaderFilenames(manager, "anime4k-c") == modeC);
    WV_REQUIRE(ShaderFilenames(manager, "anime4k-a+a") == modeAA);
    WV_REQUIRE(ShaderFilenames(manager, "anime4k-b+b") == modeBB);
    WV_REQUIRE(ShaderFilenames(manager, "anime4k-c+a") == modeCA);

    WV_REQUIRE(manager.Find("anime4k-fast") == manager.Find("anime4k-a"));
    WV_REQUIRE(manager.Find("anime4k-balanced") == manager.Find("anime4k-a"));
    WV_REQUIRE(manager.Find("anime4k-high") == manager.Find("anime4k-a"));
    WV_REQUIRE(manager.Find("anime4k-ultra") == manager.Find("anime4k-a"));
}
