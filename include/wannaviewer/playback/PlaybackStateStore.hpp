#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wannaviewer {

struct PlaybackProgress final {
    double positionSeconds{0.0};
    double durationSeconds{0.0};
    std::int64_t updatedUnixSeconds{0};
};

class PlaybackStateStore final {
public:
    static constexpr std::size_t MaximumEntries = 100;

    [[nodiscard]] static PlaybackStateStore Load(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path) const;

    [[nodiscard]] std::optional<PlaybackProgress> Find(std::string_view key) const;
    [[nodiscard]] std::optional<double> ResumePosition(std::string_view key,
                                                       double currentDurationSeconds) const;
    [[nodiscard]] static bool ShouldPersist(double positionSeconds, double durationSeconds) noexcept;
    [[nodiscard]] bool Update(std::string key, double positionSeconds, double durationSeconds);
    [[nodiscard]] bool Remove(std::string_view key);
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    void TrimToLimit();

    std::unordered_map<std::string, PlaybackProgress> entries_;
};

} // namespace wannaviewer
