#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wannaviewer {

struct ShaderPreset final {
    std::string id;
    std::string name;
    std::vector<std::string> aliases;
    int hotkey{0};
    std::vector<std::filesystem::path> shaders;
    std::string performanceClass;
};

class ShaderManager final {
public:
    ShaderManager(std::filesystem::path shaderRoot, std::filesystem::path presetFile);

    void Reload();
    [[nodiscard]] const std::vector<ShaderPreset>& Presets() const noexcept;
    [[nodiscard]] const ShaderPreset* Find(std::string_view id) const noexcept;
    [[nodiscard]] const ShaderPreset* ForHotkey(int hotkey) const noexcept;
    [[nodiscard]] std::vector<std::filesystem::path> Resolve(const ShaderPreset& preset) const;

private:
    [[nodiscard]] bool IsInsideRoot(const std::filesystem::path& candidate) const;

    std::filesystem::path shaderRoot_;
    std::filesystem::path presetFile_;
    std::vector<ShaderPreset> presets_;
};

} // namespace wannaviewer
