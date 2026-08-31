#pragma once

#include "wannaviewer/network/Resolver.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wannaviewer {

enum class RecentMediaType {
    LocalFile,
    DirectUrl,
    WebPage
};

struct RecentMediaSelection final {
    std::string seasonId;
    std::string seasonTitle;
    std::string voiceId;
    std::string voiceTitle;
    std::string episodeId;
    std::string episodeTitle;
    std::string quality;
    std::string protocol;

    bool operator==(const RecentMediaSelection&) const = default;
};

struct RecentMediaEntry final {
    std::string id;
    RecentMediaType type{RecentMediaType::LocalFile};
    std::string openValue;
    std::string title;
    std::string detail;
    std::string resumeKey;
    std::optional<RecentMediaSelection> selection;
    std::int64_t updatedUnixSeconds{0};
};

struct RecentMediaMatch final {
    std::size_t season{0};
    std::size_t voice{0};
    std::size_t episode{0};
    std::size_t stream{0};
};

[[nodiscard]] std::optional<RecentMediaMatch> MatchRecentMediaSelection(
    const AnimeEntry& entry, const RecentMediaSelection& selection);

class RecentMediaStore final {
public:
    static constexpr std::size_t MaximumEntries = 20;

    [[nodiscard]] static RecentMediaStore Load(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<RecentMediaEntry>& Entries() const noexcept;
    [[nodiscard]] const RecentMediaEntry* Find(std::string_view id) const noexcept;
    [[nodiscard]] bool Upsert(RecentMediaEntry entry);
    [[nodiscard]] bool Remove(std::string_view id);
    [[nodiscard]] bool Clear() noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;

private:
    std::vector<RecentMediaEntry> entries_;
};

} // namespace wannaviewer
