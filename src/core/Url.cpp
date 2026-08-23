#include "wannaviewer/core/Url.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>

namespace wannaviewer {
namespace {

std::string Lower(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

} // namespace

std::optional<Url> Url::Parse(std::string_view text) {
    if (text.empty() || text.size() > 16U * 1024U || text.find_first_of("\r\n\0") != std::string_view::npos) return std::nullopt;
    const auto schemeEnd = text.find("://");
    if (schemeEnd == std::string_view::npos || schemeEnd == 0) return std::nullopt;
    Url result;
    result.value_ = std::string(text);
    result.scheme_ = Lower(std::string(text.substr(0, schemeEnd)));
    if (result.scheme_ != "http" && result.scheme_ != "https") return std::nullopt;
    const auto authorityStart = schemeEnd + 3;
    const auto pathStart = text.find_first_of("/?#", authorityStart);
    auto authority = std::string(text.substr(authorityStart, pathStart - authorityStart));
    if (authority.empty() || authority.find('@') != std::string::npos) return std::nullopt;
    if (authority.front() == '[') {
        const auto closing = authority.find(']');
        if (closing == std::string::npos) return std::nullopt;
        result.host_ = Lower(authority.substr(0, closing + 1));
    } else {
        const auto port = authority.find(':');
        result.host_ = Lower(authority.substr(0, port));
    }
    if (result.host_.empty() || result.host_.find_first_of(" \\/") != std::string::npos) return std::nullopt;
    result.path_ = pathStart == std::string_view::npos ? "/" : std::string(text.substr(pathStart));
    return result;
}

bool Url::IsHttp() const noexcept { return scheme_ == "http" || scheme_ == "https"; }
bool Url::IsHttps() const noexcept { return scheme_ == "https"; }

bool Url::IsDirectMedia() const noexcept {
    auto cleanPath = path_.substr(0, path_.find_first_of("?#"));
    cleanPath = Lower(std::move(cleanPath));
    static constexpr std::array extensions{".mp4", ".m4v", ".mkv", ".webm", ".mov", ".avi", ".ts", ".m2ts", ".m3u8", ".mpd", ".flv", ".mp3", ".flac", ".opus", ".m4a"};
    return std::ranges::any_of(extensions, [&](std::string_view extension) { return cleanPath.ends_with(extension); });
}

bool Url::HostIs(std::string_view domain) const noexcept {
    const auto lowerDomain = Lower(std::string(domain));
    return host_ == lowerDomain || (host_.size() > lowerDomain.size() && host_.ends_with('.' + lowerDomain));
}

bool Url::IsPrivateHostLiteral() const noexcept {
    if (HostIs("localhost") || host_.ends_with(".local")) return true;
    if (host_.starts_with('[')) return true; // Literal IPv6 is intentionally excluded from page resolution.
    std::array<unsigned, 4> octets{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < octets.size(); ++index) {
        const auto end = index + 1 == octets.size() ? host_.size() : host_.find('.', start);
        if (end == std::string::npos || end == start) return false;
        const auto [parsed, error] = std::from_chars(host_.data() + start, host_.data() + end, octets[index]);
        if (error != std::errc{} || parsed != host_.data() + end || octets[index] > 255) return false;
        start = end + 1;
    }
    const auto a = octets[0];
    const auto b = octets[1];
    return a == 0 || a == 10 || a == 127 || (a == 100 && b >= 64 && b <= 127) ||
           (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
           (a == 192 && b == 168) || (a == 198 && (b == 18 || b == 19)) || a >= 224;
}

} // namespace wannaviewer
