#include "wannaviewer/core/Path.hpp"

namespace wannaviewer {

std::string PathToUtf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

} // namespace wannaviewer
