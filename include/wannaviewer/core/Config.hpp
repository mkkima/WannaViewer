#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace wannaviewer {

class Config final {
public:
    Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;
    Config(Config&& other) noexcept;
    Config& operator=(Config&& other) noexcept;

    static Config Load(const std::filesystem::path& path);

    [[nodiscard]] std::string GetString(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] bool GetBool(std::string_view key, bool fallback) const;
    [[nodiscard]] std::int64_t GetInt(std::string_view key, std::int64_t fallback,
                                      std::int64_t minimum, std::int64_t maximum) const;
    void Set(std::string key, std::string value);
    void Save(const std::filesystem::path& path) const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> values_;
};

} // namespace wannaviewer
