#include "wannaviewer/playback/PlaybackStateStore.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wannaviewer {
namespace {

constexpr std::uintmax_t kMaximumStateBytes = 1024U * 1024U;
constexpr double kMinimumResumePositionSeconds = 10.0;
constexpr double kMinimumMediaDurationSeconds = 60.0;

bool IsValidKey(std::string_view key) noexcept {
    return key.size() == 64 && std::ranges::all_of(key, [](unsigned char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    });
}

double CompletionMargin(double durationSeconds) noexcept {
    return std::clamp(durationSeconds * 0.05, 10.0, 30.0);
}

std::int64_t CurrentUnixSeconds() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace

PlaybackStateStore PlaybackStateStore::Load(const std::filesystem::path& path) {
    PlaybackStateStore store;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return store;
    if (error) throw std::runtime_error("Unable to inspect playback state");
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw std::runtime_error("Unable to read playback state size");
    if (size > kMaximumStateBytes) throw std::runtime_error("Playback state exceeds the size limit");

    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open playback state");
    nlohmann::json root;
    try {
        input >> root;
    } catch (const nlohmann::json::exception& exception) {
        throw std::runtime_error(std::string("Unable to parse playback state: ") + exception.what());
    }
    if (!root.is_object() || root.value("schema_version", 0) != 1 ||
        !root.contains("entries") || !root["entries"].is_object()) {
        throw std::runtime_error("Unsupported playback state schema");
    }

    for (const auto& [key, value] : root["entries"].items()) {
        if (!IsValidKey(key) || !value.is_object()) continue;
        try {
            const double position = value.value("position_seconds", -1.0);
            const double duration = value.value("duration_seconds", -1.0);
            const auto updated = value.value("updated_unix_seconds", std::int64_t{0});
            if (!std::isfinite(position) || !std::isfinite(duration) || position < 0.0 || duration <= 0.0 ||
                position > duration || updated <= 0) continue;
            store.entries_.insert_or_assign(key, PlaybackProgress{position, duration, updated});
        } catch (const nlohmann::json::exception&) {
            continue;
        }
    }
    store.TrimToLimit();
    return store;
}

void PlaybackStateStore::Save(const std::filesystem::path& path) const {
    nlohmann::json entries = nlohmann::json::object();
    for (const auto& [key, progress] : entries_) {
        entries[key] = {
            {"position_seconds", progress.positionSeconds},
            {"duration_seconds", progress.durationSeconds},
            {"updated_unix_seconds", progress.updatedUnixSeconds}
        };
    }
    const nlohmann::json root{{"schema_version", 1}, {"entries", std::move(entries)}};
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Unable to write playback state");
        output << root.dump(2) << '\n';
        output.flush();
        if (!output) throw std::runtime_error("Unable to flush playback state");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw std::runtime_error("Unable to replace playback state");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw std::runtime_error("Unable to replace playback state");
    }
#endif
}

std::optional<PlaybackProgress> PlaybackStateStore::Find(std::string_view key) const {
    const auto found = entries_.find(std::string(key));
    return found == entries_.end() ? std::nullopt : std::optional<PlaybackProgress>(found->second);
}

std::optional<double> PlaybackStateStore::ResumePosition(std::string_view key,
                                                         double currentDurationSeconds) const {
    const auto progress = Find(key);
    if (!progress || !ShouldPersist(progress->positionSeconds, progress->durationSeconds) ||
        !std::isfinite(currentDurationSeconds) || currentDurationSeconds < kMinimumMediaDurationSeconds) {
        return std::nullopt;
    }
    const double allowedDurationDifference = std::max(5.0, currentDurationSeconds * 0.05);
    if (std::abs(progress->durationSeconds - currentDurationSeconds) > allowedDurationDifference ||
        !ShouldPersist(progress->positionSeconds, currentDurationSeconds)) {
        return std::nullopt;
    }
    return progress->positionSeconds;
}

bool PlaybackStateStore::ShouldPersist(double positionSeconds, double durationSeconds) noexcept {
    return std::isfinite(positionSeconds) && std::isfinite(durationSeconds) &&
           durationSeconds >= kMinimumMediaDurationSeconds &&
           positionSeconds >= kMinimumResumePositionSeconds &&
           positionSeconds < durationSeconds - CompletionMargin(durationSeconds);
}

bool PlaybackStateStore::Update(std::string key, double positionSeconds, double durationSeconds) {
    if (!IsValidKey(key) || !ShouldPersist(positionSeconds, durationSeconds)) return false;
    entries_.insert_or_assign(std::move(key),
                              PlaybackProgress{positionSeconds, durationSeconds, CurrentUnixSeconds()});
    TrimToLimit();
    return true;
}

bool PlaybackStateStore::Remove(std::string_view key) {
    return entries_.erase(std::string(key)) != 0;
}

std::size_t PlaybackStateStore::Size() const noexcept {
    return entries_.size();
}

void PlaybackStateStore::TrimToLimit() {
    while (entries_.size() > MaximumEntries) {
        const auto oldest = std::ranges::min_element(entries_, {}, [](const auto& item) {
            return item.second.updatedUnixSeconds;
        });
        if (oldest == entries_.end()) break;
        entries_.erase(oldest);
    }
}

} // namespace wannaviewer
