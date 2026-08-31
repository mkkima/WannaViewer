#include "wannaviewer/core/AppPaths.hpp"
#include "wannaviewer/core/Path.hpp"

#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <mach-o/dyld.h>
#include <cstdlib>
#endif

namespace wannaviewer {

AppPaths AppPaths::Discover() {
    std::filesystem::path executable;
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        throw std::runtime_error("Unable to discover executable path");
    }
    buffer.resize(length);
    executable = buffer;
#else
    std::uint32_t length = 0;
    (void)_NSGetExecutablePath(nullptr, &length);
    std::string buffer(length, '\0');
    if (_NSGetExecutablePath(buffer.data(), &length) != 0) {
        throw std::runtime_error("Unable to discover executable path");
    }
    executable = std::filesystem::weakly_canonical(buffer.c_str());
#endif
    const auto root = executable.parent_path();
    auto writableRoot = root;
    if (std::filesystem::is_regular_file(root / "config" / "use-user-config")) {
#ifdef _WIN32
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
            writableRoot = std::filesystem::path(localAppData) / "WannaViewer";
            CoTaskMemFree(localAppData);
        }
#else
        if (const char* home = std::getenv("HOME")) writableRoot = std::filesystem::path(home) / "Library" / "Application Support" / "WannaViewer";
#endif
    }
    return AppPaths{executable, root, writableRoot / "config", root / "shaders", root / "presets",
                    root / "resolvers", writableRoot / "cache", writableRoot / "logs", root / "tools"};
}

void AppPaths::EnsureWritableDirectories() const {
    for (const auto& path : {config, shaders, presets, resolvers, cache, logs, tools}) {
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) {
            throw std::runtime_error("Unable to create application directory: " + PathToUtf8(path));
        }
    }
}

} // namespace wannaviewer
