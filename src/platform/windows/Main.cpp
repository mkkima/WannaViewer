#include "wannaviewer/platform/windows/QtPlayerWindow.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QStringList>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

int main(int argumentCount, char** arguments) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    QApplication application(argumentCount, arguments);
    application.setApplicationName(QStringLiteral("WannaViewer"));
    application.setOrganizationName(QStringLiteral("WannaViewer"));
    application.setStyle(QStringLiteral("Fusion"));

    bool backgroundTest = false;
    try {
        auto paths = wannaviewer::AppPaths::Discover();
        paths.EnsureWritableDirectories();
        auto config = wannaviewer::Config::Load(paths.config / "player.conf");
        QStringList rawArguments = QCoreApplication::arguments();
        if (!rawArguments.isEmpty()) rawArguments.removeFirst();
        std::vector<std::string> commandLine;
        commandLine.reserve(static_cast<std::size_t>(rawArguments.size()));
        for (const auto& argument : rawArguments) {
            const auto bytes = argument.toUtf8();
            commandLine.emplace_back(bytes.constData(), static_cast<std::size_t>(bytes.size()));
        }
        backgroundTest = std::erase(commandLine, "--background-ui-test") != 0;
        if (backgroundTest) {
            // An off-screen HWND has no DXGI output. Keep background automation
            // completely invisible and exercise demux/decode/UI state with mpv's
            // null output instead of creating an invalid D3D swap chain.
            config.Set("playback.vo", "null");
            config.Set("playback.hwdec", "no");
        }
        const bool benchmark = !commandLine.empty() && commandLine.front() == "--benchmark";
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
                    static_cast<std::uintmax_t>(config.GetInt("logging.max_bytes", 2 * 1024 * 1024,
                                                              64 * 1024, 64 * 1024 * 1024)), 3);
        logger.Write(wannaviewer::LogLevel::Info, "application", "WannaViewer starting with Qt Widgets UI");

        wannaviewer::QtPlayerWindow window(std::move(paths), std::move(config), logger, backgroundTest);
        if (backgroundTest) {
            QRect virtualDesktop;
            for (const auto* screen : QGuiApplication::screens()) virtualDesktop = virtualDesktop.united(screen->geometry());
            window.move(virtualDesktop.right() + 1024, virtualDesktop.bottom() + 1024);
        }
        window.show();
        if (benchmark) window.EnableBenchmark(std::move(input), std::move(benchmarkMode));
        else if (!input.empty()) window.OpenInitial(std::move(input));
        return application.exec();
    } catch (const std::exception& error) {
        if (!backgroundTest) {
            QMessageBox::critical(nullptr, QStringLiteral("WannaViewer"),
                                  QStringLiteral("WannaViewer could not start:\n\n") + QString::fromUtf8(error.what()));
        }
        return 1;
    }
}
