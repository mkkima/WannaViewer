#include "wannaviewer/playback/RecentMediaStore.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wannaviewer {
namespace {

constexpr std::uintmax_t kMaximumStateBytes = 1024U * 1024U;
constexpr std::size_t kMaximumOpenValueBytes = 16U * 1024U;
constexpr std::size_t kMaximumTitleBytes = 512U;
constexpr std::size_t kMaximumDetailBytes = 1024U;
constexpr std::size_t kMaximumSelectionBytes = 512U;

bool IsValidKey(std::string_view key) noexcept {
    return key.size() == 64 && std::ranges::all_of(key, [](unsigned char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
               (value >= 'A' && value <= 'F');
    });
}

bool IsBounded(std::string_view value, std::size_t maximum, bool allowEmpty = true) noexcept {
    return value.size() <= maximum && (allowEmpty || !value.empty());
}

std::int64_t CurrentUnixSeconds() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

const char* TypeName(RecentMediaType type) noexcept {
    switch (type) {
    case RecentMediaType::LocalFile: return "file";
    case RecentMediaType::DirectUrl: return "url";
    case RecentMediaType::WebPage: return "page";
    }
    return "file";
}

std::optional<RecentMediaType> ParseType(std::string_view value) noexcept {
    if (value == "file") return RecentMediaType::LocalFile;
    if (value == "url") return RecentMediaType::DirectUrl;
    if (value == "page") return RecentMediaType::WebPage;
    return std::nullopt;
}

bool IsValidSelection(const RecentMediaSelection& selection) noexcept {
    const std::array fields{
        std::string_view(selection.seasonId), std::string_view(selection.seasonTitle),
        std::string_view(selection.voiceId), std::string_view(selection.voiceTitle),
        std::string_view(selection.episodeId), std::string_view(selection.episodeTitle),
        std::string_view(selection.quality), std::string_view(selection.protocol)};
    return std::ranges::all_of(fields, [](std::string_view field) {
        return IsBounded(field, kMaximumSelectionBytes);
    });
}

bool IsValidEntry(const RecentMediaEntry& entry) {
    if (!IsValidKey(entry.id) || !IsBounded(entry.openValue, kMaximumOpenValueBytes, false) ||
        !IsBounded(entry.title, kMaximumTitleBytes, false) ||
        !IsBounded(entry.detail, kMaximumDetailBytes) ||
        (!entry.resumeKey.empty() && !IsValidKey(entry.resumeKey)) || entry.updatedUnixSeconds <= 0 ||
        (entry.selection && !IsValidSelection(*entry.selection))) return false;
    const bool selectionMatchesType = (entry.type == RecentMediaType::WebPage) == entry.selection.has_value();
    const bool validOpenValue = entry.type == RecentMediaType::LocalFile || Url::Parse(entry.openValue).has_value();
    return selectionMatchesType && validOpenValue;
}

bool MatchesComponent(std::string_view id, std::string_view title,
                      std::string_view wantedId, std::string_view wantedTitle) noexcept {
    if (wantedId.empty() && wantedTitle.empty()) return true;
    return (!wantedId.empty() && id == wantedId) || (!wantedTitle.empty() && title == wantedTitle);
}

nlohmann::json SelectionJson(const RecentMediaSelection& selection) {
    return {
        {"season_id", selection.seasonId}, {"season_title", selection.seasonTitle},
        {"voice_id", selection.voiceId}, {"voice_title", selection.voiceTitle},
        {"episode_id", selection.episodeId}, {"episode_title", selection.episodeTitle},
        {"quality", selection.quality}, {"protocol", selection.protocol}
    };
}

RecentMediaSelection ParseSelection(const nlohmann::json& value) {
    return {
        value.value("season_id", std::string{}), value.value("season_title", std::string{}),
        value.value("voice_id", std::string{}), value.value("voice_title", std::string{}),
        value.value("episode_id", std::string{}), value.value("episode_title", std::string{}),
        value.value("quality", std::string{}), value.value("protocol", std::string{})
    };
}

} // namespace

std::optional<RecentMediaMatch> MatchRecentMediaSelection(const AnimeEntry& entry,
                                                           const RecentMediaSelection& selection) {
    std::optional<RecentMediaMatch> best;
    int bestScore = -1;
    for (std::size_t seasonIndex = 0; seasonIndex < entry.seasons.size(); ++seasonIndex) {
        const auto& season = entry.seasons[seasonIndex];
        if (!MatchesComponent(season.id, season.title, selection.seasonId, selection.seasonTitle)) continue;
        for (std::size_t voiceIndex = 0; voiceIndex < season.voiceTracks.size(); ++voiceIndex) {
            const auto& voice = season.voiceTracks[voiceIndex];
            if (!MatchesComponent(voice.id, voice.title, selection.voiceId, selection.voiceTitle)) continue;
            for (std::size_t episodeIndex = 0; episodeIndex < voice.episodes.size(); ++episodeIndex) {
                const auto& episode = voice.episodes[episodeIndex];
                if (!MatchesComponent(episode.id, episode.title,
                                      selection.episodeId, selection.episodeTitle)) continue;
                for (std::size_t streamIndex = 0; streamIndex < episode.streams.size(); ++streamIndex) {
                    const auto& stream = episode.streams[streamIndex];
                    if (stream.protectedStream) continue;
                    int score = 0;
                    if (!selection.quality.empty() && stream.quality == selection.quality) score += 2;
                    if (!selection.protocol.empty() && stream.protocol == selection.protocol) score += 1;
                    if (score > bestScore) {
                        best = RecentMediaMatch{seasonIndex, voiceIndex, episodeIndex, streamIndex};
                        bestScore = score;
                    }
                }
            }
        }
    }
    return best;
}

RecentMediaStore RecentMediaStore::Load(const std::filesystem::path& path) {
    RecentMediaStore store;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return store;
    if (error) throw std::runtime_error("Unable to inspect recent media state");
    const auto size = std::filesystem::file_size(path, error);
    if (error) throw std::runtime_error("Unable to read recent media state size");
    if (size > kMaximumStateBytes) throw std::runtime_error("Recent media state exceeds the size limit");

    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open recent media state");
    nlohmann::json root;
    try {
        input >> root;
    } catch (const nlohmann::json::exception& exception) {
        throw std::runtime_error(std::string("Unable to parse recent media state: ") + exception.what());
    }
    if (!root.is_object() || root.value("schema_version", 0) != 1 ||
        !root.contains("entries") || !root["entries"].is_array()) {
        throw std::runtime_error("Unsupported recent media state schema");
    }

    std::unordered_set<std::string> ids;
    for (const auto& value : root["entries"]) {
        if (!value.is_object()) continue;
        try {
            const auto type = ParseType(value.value("type", std::string{}));
            if (!type) continue;
            RecentMediaEntry entry{
                value.value("id", std::string{}), *type,
                value.value("open_value", std::string{}), value.value("title", std::string{}),
                value.value("detail", std::string{}), value.value("resume_key", std::string{}),
                std::nullopt, value.value("updated_unix_seconds", std::int64_t{0})
            };
            if (value.contains("selection")) {
                if (!value["selection"].is_object()) continue;
                entry.selection = ParseSelection(value["selection"]);
            }
            if (!IsValidEntry(entry) || !ids.insert(entry.id).second) continue;
            store.entries_.push_back(std::move(entry));
        } catch (const nlohmann::json::exception&) {
            continue;
        }
    }
    std::ranges::stable_sort(store.entries_, std::greater{}, &RecentMediaEntry::updatedUnixSeconds);
    if (store.entries_.size() > MaximumEntries) store.entries_.resize(MaximumEntries);
    return store;
}

void RecentMediaStore::Save(const std::filesystem::path& path) const {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& entry : entries_) {
        nlohmann::json value{
            {"id", entry.id}, {"type", TypeName(entry.type)}, {"open_value", entry.openValue},
            {"title", entry.title}, {"detail", entry.detail}, {"resume_key", entry.resumeKey},
            {"updated_unix_seconds", entry.updatedUnixSeconds}
        };
        if (entry.selection) value["selection"] = SelectionJson(*entry.selection);
        entries.push_back(std::move(value));
    }
    const nlohmann::json root{{"schema_version", 1}, {"entries", std::move(entries)}};
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Unable to write recent media state");
        output << root.dump(2) << '\n';
        output.flush();
        if (!output) throw std::runtime_error("Unable to flush recent media state");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw std::runtime_error("Unable to replace recent media state");
    }
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        throw std::runtime_error("Unable to replace recent media state");
    }
#endif
}

const std::vector<RecentMediaEntry>& RecentMediaStore::Entries() const noexcept {
    return entries_;
}

const RecentMediaEntry* RecentMediaStore::Find(std::string_view id) const noexcept {
    const auto found = std::ranges::find(entries_, id, &RecentMediaEntry::id);
    return found == entries_.end() ? nullptr : &*found;
}

bool RecentMediaStore::Upsert(RecentMediaEntry entry) {
    entry.updatedUnixSeconds = CurrentUnixSeconds();
    if (!IsValidEntry(entry)) return false;
    const auto found = std::ranges::find(entries_, entry.id, &RecentMediaEntry::id);
    const bool existed = found != entries_.end();
    const bool changed = found == entries_.end() || found->type != entry.type ||
        found->openValue != entry.openValue || found->title != entry.title || found->detail != entry.detail ||
        found->resumeKey != entry.resumeKey || found->selection != entry.selection;
    if (found != entries_.end()) entries_.erase(found);
    entries_.insert(entries_.begin(), std::move(entry));
    if (entries_.size() > MaximumEntries) entries_.resize(MaximumEntries);
    return changed || existed;
}

bool RecentMediaStore::Remove(std::string_view id) {
    const auto found = std::ranges::find(entries_, id, &RecentMediaEntry::id);
    if (found == entries_.end()) return false;
    entries_.erase(found);
    return true;
}

bool RecentMediaStore::Clear() noexcept {
    if (entries_.empty()) return false;
    entries_.clear();
    return true;
}

std::size_t RecentMediaStore::Size() const noexcept {
    return entries_.size();
}

} // namespace wannaviewer
