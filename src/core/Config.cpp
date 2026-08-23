#include "wannaviewer/core/Config.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <mutex>
#include <stdexcept>

namespace wannaviewer {
namespace {

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n'; };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

} // namespace

Config::Config(Config&& other) noexcept {
    std::unique_lock lock(other.mutex_);
    values_ = std::move(other.values_);
}

Config& Config::operator=(Config&& other) noexcept {
    if (this == &other) return *this;
    std::scoped_lock lock(mutex_, other.mutex_);
    values_ = std::move(other.values_);
    return *this;
}

Config Config::Load(const std::filesystem::path& path) {
    Config config;
    std::ifstream input(path);
    if (!input) {
        return config;
    }
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        auto key = Trim(line.substr(0, separator));
        auto value = Trim(line.substr(separator + 1));
        if (!key.empty()) {
            config.values_.insert_or_assign(std::move(key), std::move(value));
        }
    }
    return config;
}

std::string Config::GetString(std::string_view key, std::string_view fallback) const {
    std::shared_lock lock(mutex_);
    const auto item = values_.find(std::string(key));
    return item == values_.end() ? std::string(fallback) : item->second;
}

bool Config::GetBool(std::string_view key, bool fallback) const {
    auto value = GetString(key);
    std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (value == "true" || value == "yes" || value == "1" || value == "on") return true;
    if (value == "false" || value == "no" || value == "0" || value == "off") return false;
    return fallback;
}

std::int64_t Config::GetInt(std::string_view key, std::int64_t fallback,
                            std::int64_t minimum, std::int64_t maximum) const {
    const auto text = GetString(key);
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) return fallback;
    return std::clamp(value, minimum, maximum);
}

void Config::Set(std::string key, std::string value) {
    std::unique_lock lock(mutex_);
    values_.insert_or_assign(std::move(key), std::move(value));
}

void Config::Save(const std::filesystem::path& path) const {
    std::shared_lock lock(mutex_);
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("Unable to write configuration");
    std::vector<std::pair<std::string, std::string>> sorted(values_.begin(), values_.end());
    std::ranges::sort(sorted);
    for (const auto& [key, value] : sorted) output << key << '=' << value << '\n';
    output.flush();
    if (!output) throw std::runtime_error("Unable to flush configuration");
    output.close();
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
    }
    if (error) throw std::runtime_error("Unable to replace configuration");
}

} // namespace wannaviewer
