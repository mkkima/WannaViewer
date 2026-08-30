#include "wannaviewer/platform/windows/QtPlayerWindow.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QStringList>
#include <QSurfaceFormat>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

int main(int argumentCount, char** arguments) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    // Qt 6.8's Windows plugin caches WinRT factories whose vtables live in
    // Windows.UI.dll, then clears them from QWindowsContext's destructor. Some
    // media/COM shutdown paths can release the last normal loader reference
    // first, leaving Qt with a pointer into an unloaded module. Retain the
    // system DLL until process teardown so QApplication can clear its cache
    // while the factory code is still mapped.
    [[maybe_unused]] const HMODULE windowsUiLifetime =
        LoadLibraryExW(L"Windows.UI.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat videoFormat;
    videoFormat.setRenderableType(QSurfaceFormat::OpenGL);
    videoFormat.setVersion(3, 3);
    videoFormat.setProfile(QSurfaceFormat::CoreProfile);
    videoFormat.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    videoFormat.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(videoFormat);
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
        const bool backgroundUiTest = std::erase(commandLine, "--background-ui-test") != 0;
        const bool backgroundRenderTest = std::erase(commandLine, "--background-render-test") != 0;
        const bool backgroundMotionTest = std::erase(commandLine, "--background-motion-test") != 0;
        backgroundTest = backgroundUiTest || backgroundRenderTest || backgroundMotionTest;
        if (backgroundRenderTest) config.Set("logging.level", "debug");
        if (backgroundUiTest || backgroundMotionTest) {
            // Keep layout/input automation completely off-screen and use mpv's
            // null output. The separate render smoke covers the real OpenGL path.
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

        wannaviewer::QtPlayerWindow window(std::move(paths), std::move(config), logger,
                                           backgroundTest, backgroundMotionTest);
        if (backgroundUiTest || backgroundMotionTest) {
            QRect virtualDesktop;
            for (const auto* screen : QGuiApplication::screens()) virtualDesktop = virtualDesktop.united(screen->geometry());
            window.move(virtualDesktop.right() + 1024, virtualDesktop.bottom() + 1024);
        } else if (backgroundRenderTest) {
            window.setWindowFlag(Qt::Tool, true);
            window.setAttribute(Qt::WA_TransparentForMouseEvents, true);
            // A fully transparent top-level is culled by DWM and Qt will stop
            // scheduling QOpenGLWidget paints. A 1%-opaque 64px tool window is
            // effectively invisible while still exercising the real compositor.
            window.setWindowOpacity(0.01);
            window.setMinimumSize(1, 1);
            window.resize(64, 64);
            if (const auto* screen = QGuiApplication::primaryScreen()) {
                const auto area = screen->availableGeometry();
                window.move(area.right() - window.width() + 1, area.bottom() - window.height() + 1);
            }
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
