#include "wannaviewer/platform/windows/PlayerWindow.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include <commctrl.h>
#include <shellapi.h>
#include <windows.h>

namespace {

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    bool backgroundTest = false;
    try {
        auto paths = wannaviewer::AppPaths::Discover();
        paths.EnsureWritableDirectories();
        auto config = wannaviewer::Config::Load(paths.config / "player.conf");
        int argumentCount = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        std::vector<std::string> commandLine;
        for (int index = 1; arguments && index < argumentCount; ++index) commandLine.push_back(WideToUtf8(arguments[index]));
        if (arguments) LocalFree(arguments);
        backgroundTest = std::erase(commandLine, "--background-ui-test") != 0;
        bool benchmark = !commandLine.empty() && commandLine.front() == "--benchmark";
        std::string benchmarkMode = "hardware";
        std::string input;
        if (benchmark) {
            for (std::size_t index = 1; index < commandLine.size(); ++index) {
                if (commandLine[index].starts_with("--benchmark-mode=")) benchmarkMode = commandLine[index].substr(17);
                else if (input.empty()) input = commandLine[index];
            }
            if (input.empty()) throw std::runtime_error("--benchmark requires a media path or direct URL");
            if (benchmarkMode != "software" && benchmarkMode != "hardware" && benchmarkMode != "hardware-shader")
                throw std::runtime_error("--benchmark-mode must be software, hardware, or hardware-shader");
            config.Set("playback.hwdec", benchmarkMode == "software" ? "no" : "auto");
        } else if (!commandLine.empty()) {
            input = commandLine.front();
        }
        wannaviewer::Logger logger;
        logger.Open(paths.logs, wannaviewer::ParseLogLevel(config.GetString("logging.level", "info")),
                    static_cast<std::uintmax_t>(config.GetInt("logging.max_bytes", 2 * 1024 * 1024, 64 * 1024, 64 * 1024 * 1024)), 3);
        logger.Write(wannaviewer::LogLevel::Info, "application", "WannaViewer starting");

        wannaviewer::PlayerWindow window(std::move(paths), std::move(config), logger);
        window.Create(instance, showCommand, backgroundTest);
        if (benchmark) window.EnableBenchmark(std::move(input), std::move(benchmarkMode));
        else if (!input.empty()) window.OpenInitial(std::move(input));
        return window.Run();
    } catch (const std::exception& error) {
        const auto detail = std::string("WannaViewer could not start:\n\n") + error.what();
        const auto wide = [&] {
            const int count = MultiByteToWideChar(CP_UTF8, 0, detail.data(), static_cast<int>(detail.size()), nullptr, 0);
            std::wstring value(static_cast<std::size_t>(count), L'\0');
            (void)MultiByteToWideChar(CP_UTF8, 0, detail.data(), static_cast<int>(detail.size()), value.data(), count);
            return value;
        }();
        if (!backgroundTest) MessageBoxW(nullptr, wide.c_str(), L"WannaViewer", MB_OK | MB_ICONERROR);
        return 1;
    }
}
