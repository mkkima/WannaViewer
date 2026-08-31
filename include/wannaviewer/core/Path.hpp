#pragma once

#include <filesystem>
#include <string>

namespace wannaviewer {

[[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);

} // namespace wannaviewer
