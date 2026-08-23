#include "wannaviewer/core/Redaction.hpp"

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

} // namespace

std::string RedactSecrets(std::string_view input) {
    std::string result(input);
    for (const auto marker : {"authorization:", "proxy-authorization:", "cookie:", "set-cookie:",
                              "token=", "token_movie=", "access_token=", "refresh_token=", "auth=",
                              "signature=", "sig=", "apikey=", "api_key="}) {
        RedactAfter(result, marker);
    }
    return result;
}

} // namespace wannaviewer
