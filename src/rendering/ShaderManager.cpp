#include "wannaviewer/rendering/ShaderManager.hpp"
#include "wannaviewer/core/Path.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace wannaviewer {

ShaderManager::ShaderManager(std::filesystem::path shaderRoot, std::filesystem::path presetFile)
    : shaderRoot_(std::filesystem::weakly_canonical(std::move(shaderRoot))), presetFile_(std::move(presetFile)) {}

void ShaderManager::Reload() {
    std::ifstream input(presetFile_);
    if (!input) throw std::runtime_error("Shader preset file is missing: " + PathToUtf8(presetFile_));
    const auto document = nlohmann::json::parse(input, nullptr, true, true);
    std::vector<ShaderPreset> loaded;
    for (const auto& item : document.at("presets")) {
        ShaderPreset preset;
        preset.id = item.at("id").get<std::string>();
        preset.name = item.at("name").get<std::string>();
        preset.hotkey = item.value("hotkey", 0);
        preset.performanceClass = item.value("performance_class", "custom");
        if (preset.id.empty() || preset.name.empty() || preset.hotkey < 0 || preset.hotkey > 9)
            throw std::runtime_error("Invalid shader preset metadata");
        for (const auto& shader : item.value("shaders", nlohmann::json::array()))
            preset.shaders.emplace_back(shader.get<std::string>());
        loaded.push_back(std::move(preset));
    }
    if (loaded.empty() || loaded.front().id != "off")
        throw std::runtime_error("The first shader preset must be 'off'");
    presets_ = std::move(loaded);
}

const std::vector<ShaderPreset>& ShaderManager::Presets() const noexcept { return presets_; }

const ShaderPreset* ShaderManager::Find(std::string_view id) const noexcept {
    for (const auto& preset : presets_) if (preset.id == id) return &preset;
    return nullptr;
}

const ShaderPreset* ShaderManager::ForHotkey(int hotkey) const noexcept {
    for (const auto& preset : presets_) if (preset.hotkey == hotkey) return &preset;
    return nullptr;
}

bool ShaderManager::IsInsideRoot(const std::filesystem::path& candidate) const {
    const auto root = shaderRoot_.lexically_normal();
    const auto path = candidate.lexically_normal();
    auto rootPart = root.begin();
    auto pathPart = path.begin();
    for (; rootPart != root.end(); ++rootPart, ++pathPart)
        if (pathPart == path.end() || *rootPart != *pathPart) return false;
    return true;
}

std::vector<std::filesystem::path> ShaderManager::Resolve(const ShaderPreset& preset) const {
    std::vector<std::filesystem::path> result;
    for (const auto& relative : preset.shaders) {
        if (relative.is_absolute()) throw std::runtime_error("Absolute shader paths are not allowed in presets");
        const auto full = std::filesystem::weakly_canonical(shaderRoot_ / relative);
        if (!IsInsideRoot(full)) throw std::runtime_error("Shader preset escapes the shader directory");
        if (!std::filesystem::is_regular_file(full))
            throw std::runtime_error("Shader is missing: " + PathToUtf8(relative));
        if (full.extension() != ".glsl") throw std::runtime_error("Only .glsl shader files are accepted");
        result.push_back(full);
    }
    return result;
}

} // namespace wannaviewer
