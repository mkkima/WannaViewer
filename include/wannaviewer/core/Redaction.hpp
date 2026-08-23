#pragma once

#include <string>
#include <string_view>

namespace wannaviewer {

[[nodiscard]] std::string RedactSecrets(std::string_view input);

} // namespace wannaviewer
