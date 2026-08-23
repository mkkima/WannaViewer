#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace wannaviewer {

class Url final {
public:
    static std::optional<Url> Parse(std::string_view text);

    [[nodiscard]] const std::string& Value() const noexcept { return value_; }
    [[nodiscard]] const std::string& Scheme() const noexcept { return scheme_; }
    [[nodiscard]] const std::string& Host() const noexcept { return host_; }
    [[nodiscard]] const std::string& Path() const noexcept { return path_; }
    [[nodiscard]] bool IsHttp() const noexcept;
    [[nodiscard]] bool IsHttps() const noexcept;
    [[nodiscard]] bool IsDirectMedia() const noexcept;
    [[nodiscard]] bool HostIs(std::string_view domain) const noexcept;
    [[nodiscard]] bool IsPrivateHostLiteral() const noexcept;

private:
    std::string value_;
    std::string scheme_;
    std::string host_;
    std::string path_;
};

} // namespace wannaviewer
