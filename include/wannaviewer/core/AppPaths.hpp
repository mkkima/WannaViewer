#pragma once

#include <filesystem>

namespace wannaviewer {

struct AppPaths final {
    std::filesystem::path executable;
    std::filesystem::path root;
    std::filesystem::path config;
    std::filesystem::path shaders;
    std::filesystem::path presets;
    std::filesystem::path resolvers;
    std::filesystem::path cache;
    std::filesystem::path logs;
    std::filesystem::path tools;

    [[nodiscard]] static AppPaths Discover();
    void EnsureWritableDirectories() const;
};

} // namespace wannaviewer
