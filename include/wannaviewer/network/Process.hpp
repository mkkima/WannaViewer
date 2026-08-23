#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <string>
#include <vector>

namespace wannaviewer {

struct ProcessResult final {
    unsigned exitCode{0};
    std::string output;
    bool timedOut{false};
    bool cancelled{false};
    bool outputTruncated{false};
};

[[nodiscard]] ProcessResult RunProcess(const std::filesystem::path& executable,
                                       const std::vector<std::string>& arguments,
                                       std::chrono::milliseconds timeout,
                                       std::size_t maximumOutputBytes,
                                       std::stop_token stopToken = {});

} // namespace wannaviewer
