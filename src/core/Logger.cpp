#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/core/Redaction.hpp"

#include <array>
#include <chrono>
#include <format>
#include <system_error>

namespace wannaviewer {

Logger::~Logger() {
    std::scoped_lock lock(mutex_);
    if (stream_) stream_.flush();
}

void Logger::Open(const std::filesystem::path& logDirectory, LogLevel level,
                  std::uintmax_t maxBytes, unsigned retainedFiles) {
    std::scoped_lock lock(mutex_);
    std::filesystem::create_directories(logDirectory);
    path_ = logDirectory / "wannaviewer.log";
    level_ = level;
    maxBytes_ = maxBytes;
    retainedFiles_ = retainedFiles;
    RotateIfNeededLocked();
    stream_.open(path_, std::ios::app);
}

void Logger::Write(LogLevel level, std::string_view component, std::string_view message) {
    if (level == LogLevel::Off || static_cast<int>(level) > static_cast<int>(level_)) return;
    static constexpr std::array names{"OFF", "ERROR", "INFO", "DEBUG", "TRACE"};
    const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    std::scoped_lock lock(mutex_);
    if (!stream_) return;
    RotateIfNeededLocked();
    if (!stream_.is_open()) stream_.open(path_, std::ios::app);
    stream_ << std::format("{:%FT%T%Ez} [{}] [{}] {}\n", now, names.at(static_cast<std::size_t>(level)),
                           component, RedactSecrets(message));
    if (level == LogLevel::Error) stream_.flush();
}

LogLevel Logger::Level() const noexcept {
    std::scoped_lock lock(mutex_);
    return level_;
}

void Logger::RotateIfNeededLocked() {
    std::error_code error;
    if (path_.empty() || !std::filesystem::exists(path_, error) ||
        std::filesystem::file_size(path_, error) < maxBytes_) return;
    if (stream_.is_open()) stream_.close();
    if (retainedFiles_ == 0) {
        std::filesystem::remove(path_, error);
        return;
    }
    for (unsigned index = retainedFiles_; index > 0; --index) {
        const auto destination = path_.string() + '.' + std::to_string(index);
        const auto source = index == 1 ? path_ : std::filesystem::path(path_.string() + '.' + std::to_string(index - 1));
        std::filesystem::remove(destination, error);
        error.clear();
        if (std::filesystem::exists(source, error)) std::filesystem::rename(source, destination, error);
        error.clear();
    }
}

LogLevel ParseLogLevel(std::string_view text) noexcept {
    if (text == "off") return LogLevel::Off;
    if (text == "error") return LogLevel::Error;
    if (text == "debug") return LogLevel::Debug;
    if (text == "trace") return LogLevel::Trace;
    return LogLevel::Info;
}

} // namespace wannaviewer
