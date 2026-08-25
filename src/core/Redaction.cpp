#include "wannaviewer/core/Redaction.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace wannaviewer {
namespace {

bool IsDelimiter(char ch) noexcept {
    return ch == '&' || ch == ';' || ch == '\r' || ch == '\n' || ch == ' ' || ch == '"' || ch == '\'';
}

void RedactAfter(std::string& value, std::string_view marker) {
    std::string lower = value;
    for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::size_t position = 0;
    while ((position = lower.find(marker, position)) != std::string::npos) {
        auto start = position + marker.size();
        while (start < value.size() && (value[start] == ' ' || value[start] == '\t')) ++start;
        auto end = start;
        if (marker.ends_with(':')) {
            while (end < value.size() && value[end] != '\r' && value[end] != '\n') ++end;
        } else {
            while (end < value.size() && !IsDelimiter(value[end])) ++end;
        }
        value.replace(start, end - start, "<redacted>");
        lower.replace(start, end - start, "<redacted>");
        position = start + 10;
    }
}

void RedactOpaqueUrlPathSegments(std::string& value) {
    std::size_t search = 0;
    while (search < value.size()) {
        const auto https = value.find("https://", search);
        const auto http = value.find("http://", search);
        auto scheme = std::min(https, http);
        if (https == std::string::npos) scheme = http;
        if (http == std::string::npos) scheme = https;
        if (scheme == std::string::npos) break;
        auto urlEnd = value.find_first_of(" \r\n\"'", scheme);
        if (urlEnd == std::string::npos) urlEnd = value.size();
        const bool isHttps = value.compare(scheme, 8, "https://") == 0;
        const auto path = value.find('/', scheme + (isHttps ? 8U : 7U));
        if (path == std::string::npos || path >= urlEnd) {
            search = urlEnd;
            continue;
        }
        std::size_t segment = path + 1;
        while (segment < urlEnd) {
            auto end = segment;
            while (end < urlEnd && value[end] != '/' && value[end] != '?' && value[end] != '#') ++end;
            if (end - segment > 96) {
                std::string suffix;
                const auto dot = value.rfind('.', end - 1);
                if (dot != std::string::npos && dot >= segment && end - dot <= 10) suffix = value.substr(dot, end - dot);
                const auto replacement = "<redacted>" + suffix;
                const auto oldLength = end - segment;
                value.replace(segment, end - segment, replacement);
                end = segment + replacement.size();
                urlEnd -= oldLength - replacement.size();
            }
            if (end >= urlEnd || value[end] != '/') {
                search = urlEnd;
                break;
            }
            segment = end + 1;
        }
        if (segment >= urlEnd) search = urlEnd;
        if (search <= scheme) search = scheme + 1;
    }
}

} // namespace

std::string RedactSecrets(std::string_view input) {
    std::string result(input);
    for (const auto marker : {"authorization:", "proxy-authorization:", "cookie:", "set-cookie:",
                              "token=", "token_movie=", "access_token=", "refresh_token=", "auth=",
                              "signature=", "sig=", "apikey=", "api_key="}) {
        RedactAfter(result, marker);
    }
    RedactOpaqueUrlPathSegments(result);
    return result;
}

} // namespace wannaviewer
