#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace wannaviewer {

enum class LogLevel { Off = 0, Error = 1, Info = 2, Debug = 3, Trace = 4 };

class Logger final {
public:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Open(const std::filesystem::path& logDirectory, LogLevel level,
              std::uintmax_t maxBytes = 2U * 1024U * 1024U, unsigned retainedFiles = 3);
    void Write(LogLevel level, std::string_view component, std::string_view message);
    [[nodiscard]] LogLevel Level() const noexcept;

private:
    void RotateIfNeededLocked();

    mutable std::mutex mutex_;
    std::ofstream stream_;
    std::filesystem::path path_;
    LogLevel level_{LogLevel::Off};
    std::uintmax_t maxBytes_{0};
    unsigned retainedFiles_{0};
};

[[nodiscard]] LogLevel ParseLogLevel(std::string_view text) noexcept;

} // namespace wannaviewer
