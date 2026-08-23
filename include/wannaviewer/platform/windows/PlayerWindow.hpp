#pragma once

#ifdef _WIN32

#include "wannaviewer/core/AppPaths.hpp"
#include "wannaviewer/core/Config.hpp"
#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/network/HttpClient.hpp"
#include "wannaviewer/network/ResolverPipeline.hpp"
#include "wannaviewer/network/YtDlpBridge.hpp"
#include "wannaviewer/playback/MpvEngine.hpp"
#include "wannaviewer/rendering/ShaderManager.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>

namespace wannaviewer {

class PlayerWindow final {
public:
    PlayerWindow(AppPaths paths, Config config, Logger& logger);
    ~PlayerWindow();
    PlayerWindow(const PlayerWindow&) = delete;
    PlayerWindow& operator=(const PlayerWindow&) = delete;

    void Create(HINSTANCE instance, int showCommand);
    int Run();
    void OpenInitial(std::string value);
    void EnableBenchmark(std::string value, std::string mode);

private:
    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void LayoutControls();
    void ShowControls(bool show);
    void RecordInteraction();
    void UpdateActiveTimer();
    void UpdateUi();
    void UpdateTracks();
    void ToggleFullscreen();
    void ToggleStatistics();
    void ApplyShaderHotkey(int hotkey);
    void ApplyShaderPreset(std::size_t index);
    void OpenCustomShaders();
    void ShowSettingsMenu();
    void OpenFileDialog();
    void OpenUrlDialog();
    void ResolveUrl(std::string value, HeaderMap inheritedHeaders = {});
    void HandleResolveResult(ResolveResult result);
    void OpenVariant(const StreamVariant& stream);
    void HandlePlaybackEvent(PlaybackEvent event);
    void ShowError(std::wstring_view title, std::string_view detail);
    void LogHardwareInformation();
    void FinishBenchmark();

    AppPaths paths_;
    Config config_;
    Logger& logger_;
    ShaderManager shaders_;
    HttpClient http_;
    YtDlpBridge ytDlp_;
    ResolverPipeline resolvers_;
    MpvEngine engine_;

    HINSTANCE instance_{nullptr};
    HWND window_{nullptr};
    HWND video_{nullptr};
    HWND controlsBar_{nullptr};
    HWND playButton_{nullptr};
    HWND timeline_{nullptr};
    HWND timeLabel_{nullptr};
    HWND volume_{nullptr};
    HWND audio_{nullptr};
    HWND subtitles_{nullptr};
    HWND videoQuality_{nullptr};
    HWND shader_{nullptr};
    HWND fullscreenButton_{nullptr};
    HWND settingsButton_{nullptr};
    HWND stats_{nullptr};
    HFONT font_{nullptr};

    std::vector<std::int64_t> audioTrackIds_;
    std::vector<std::int64_t> subtitleTrackIds_;
    std::vector<std::int64_t> videoTrackIds_;
    std::jthread resolverThread_;
    std::atomic_bool closing_{false};
    std::atomic_bool resolving_{false};
    std::optional<std::string> initial_;
    WINDOWPLACEMENT previousPlacement_{sizeof(WINDOWPLACEMENT)};
    DWORD previousStyle_{0};
    ULONGLONG lastInteraction_{0};
    unsigned timerTick_{0};
    unsigned benchmarkTick_{0};
    bool controlsVisible_{true};
    bool statisticsVisible_{false};
    bool fullscreen_{false};
    bool timelineDragging_{false};
    bool benchmarkMode_{false};
    bool benchmarkRunning_{false};
    std::string benchmarkInput_;
    std::string benchmarkProfile_{"hardware"};
    std::chrono::steady_clock::time_point benchmarkStart_{};
    std::vector<PlaybackStatistics> benchmarkSamples_;
    std::int64_t benchmarkDroppedBaseline_{-1};
    std::int64_t benchmarkDelayedBaseline_{-1};
};

} // namespace wannaviewer

#endif
