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
#include <utility>
#include <vector>

#include <windows.h>
#include <commctrl.h>

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
    enum class OverlayMode { None, Choice, Url, Message };
    enum class OverlayAction { None, Audio, Subtitles, Video, Shaders, Settings };

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void CreateFonts();
    void EnsureEngineInitialized();
    void OpenMedia(std::string value,
                   std::vector<std::pair<std::string, std::string>> headers = {},
                   std::string externalAudioUrl = {});
    void LayoutControls();
    void AddTooltip(HWND control, const wchar_t* text);
    void SetMediaLoaded(bool loaded);
    void ShowControls(bool show);
    void RecordInteraction();
    void RecordMouseMovement();
    [[nodiscard]] bool IsCursorOverControls() const noexcept;
    void UpdateActiveTimer();
    void UpdateUi();
    void UpdateTracks();
    void ToggleFullscreen();
    void ToggleStatistics();
    void ShowAudioMenu();
    void ShowSubtitleMenu();
    void ShowVideoMenu();
    void ShowShaderMenu();
    void ApplyShaderHotkey(int hotkey);
    void ApplyShaderPreset(std::size_t index);
    void OpenCustomShaders();
    void ShowSettingsMenu();
    void ShowChoiceOverlay(std::wstring title, std::wstring hint, std::vector<std::wstring> choices,
                           int selected, OverlayAction action);
    void ShowUrlOverlay();
    void ShowMessageOverlay(std::wstring title, std::wstring detail);
    void HideOverlay();
    void ApplyOverlaySelection();
    void OpenFileDialog();
    void OpenUrlDialog();
    void ResolveUrl(std::string value, HeaderMap inheritedHeaders = {});
    void HandleResolveResult(ResolveResult result);
    void ShowSourceSelector(ResolveResult result);
    void HideSourceSelector();
    void PopulateSourceSeasons();
    void PopulateSourceVoices();
    void PopulateSourceEpisodes();
    void PopulateSourceStreams();
    void OpenSelectedSource();
    [[nodiscard]] const StreamVariant* SelectedSource() const;
    void OpenVariant(const StreamVariant& stream);
    bool RetryBrowserProvider();
    void HandlePlaybackEvent(PlaybackEvent event);
    void ShowError(std::wstring_view title, std::string_view detail);
    void LogHardwareInformation();
    void FinishBenchmark();
    LRESULT DrawControl(const DRAWITEMSTRUCT& item);
    LRESULT DrawTrackbar(NMCUSTOMDRAW& customDraw);
    void DrawPlayerIcon(HDC dc, UINT id, const RECT& rectangle, bool enabled);
    [[nodiscard]] int Scale(int value) const noexcept;

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
    HWND rewindButton_{nullptr};
    HWND forwardButton_{nullptr};
    HWND muteButton_{nullptr};
    HWND timeline_{nullptr};
    HWND timeLabel_{nullptr};
    HWND volume_{nullptr};
    HWND audio_{nullptr};
    HWND subtitles_{nullptr};
    HWND videoQuality_{nullptr};
    HWND shader_{nullptr};
    HWND statsButton_{nullptr};
    HWND fullscreenButton_{nullptr};
    HWND settingsButton_{nullptr};
    HWND stats_{nullptr};
    HWND emptyState_{nullptr};
    HWND openFileButton_{nullptr};
    HWND openUrlButton_{nullptr};
    HWND sourcePanel_{nullptr};
    HWND sourceTitle_{nullptr};
    HWND sourceSubtitle_{nullptr};
    HWND sourceSeasonLabel_{nullptr};
    HWND sourceVoiceLabel_{nullptr};
    HWND sourceEpisodeLabel_{nullptr};
    HWND sourceStreamLabel_{nullptr};
    HWND sourceSeasonList_{nullptr};
    HWND sourceVoiceList_{nullptr};
    HWND sourceEpisodeList_{nullptr};
    HWND sourceStreamList_{nullptr};
    HWND sourceStatus_{nullptr};
    HWND sourceOpenButton_{nullptr};
    HWND sourceCancelButton_{nullptr};
    HWND overlayPanel_{nullptr};
    HWND overlayTitle_{nullptr};
    HWND overlayBody_{nullptr};
    HWND overlayEdit_{nullptr};
    HWND overlayList_{nullptr};
    HWND overlayPrimaryButton_{nullptr};
    HWND overlaySecondaryButton_{nullptr};
    HWND tooltip_{nullptr};
    HFONT font_{nullptr};
    HFONT titleFont_{nullptr};
    HBRUSH backgroundBrush_{nullptr};
    HBRUSH panelBrush_{nullptr};
    HBRUSH statisticsBrush_{nullptr};
    ULONG_PTR gdiplusToken_{0};

    std::vector<std::int64_t> audioTrackIds_;
    std::vector<std::int64_t> subtitleTrackIds_;
    std::vector<std::int64_t> videoTrackIds_;
    std::vector<std::wstring> audioTrackLabels_;
    std::vector<std::wstring> subtitleTrackLabels_;
    std::vector<std::wstring> videoTrackLabels_;
    std::jthread resolverThread_;
    std::atomic_bool closing_{false};
    std::atomic_bool resolving_{false};
    std::optional<std::string> initial_;
    std::optional<ResolveResult> sourceSelection_;
    OverlayMode overlayMode_{OverlayMode::None};
    OverlayAction overlayAction_{OverlayAction::None};
    std::vector<std::wstring> overlayChoices_;
    int overlaySelection_{-1};
    int sourceSeasonIndex_{-1};
    int sourceVoiceIndex_{-1};
    int sourceEpisodeIndex_{-1};
    int sourceStreamIndex_{-1};
    WINDOWPLACEMENT previousPlacement_{sizeof(WINDOWPLACEMENT)};
    DWORD previousStyle_{0};
    ULONGLONG lastInteraction_{0};
    POINT lastMousePosition_{};
    unsigned timerTick_{0};
    unsigned benchmarkTick_{0};
    unsigned dpi_{96};
    std::size_t shaderPresetIndex_{0};
    int audioSelection_{-1};
    int subtitleSelection_{0};
    int videoSelection_{-1};
    bool hasLastMousePosition_{false};
    bool controlsVisible_{true};
    bool statisticsVisible_{false};
    bool mediaLoaded_{false};
    bool mediaOpening_{false};
    bool playbackStarted_{false};
    bool startupTimeoutReported_{false};
    bool compactLayout_{false};
    bool fullscreen_{false};
    bool timelineDragging_{false};
    bool timelineHovering_{false};
    int timelineHoverX_{0};
    bool benchmarkMode_{false};
    bool benchmarkRunning_{false};
    std::string benchmarkInput_;
    std::string benchmarkProfile_{"hardware"};
    std::string browserRetryUrl_;
    HeaderMap browserRetryHeaders_;
    unsigned browserRetriesRemaining_{0};
    std::chrono::steady_clock::time_point benchmarkStart_{};
    std::chrono::steady_clock::time_point playbackLoadStarted_{};
    std::vector<PlaybackStatistics> benchmarkSamples_;
    std::int64_t benchmarkDroppedBaseline_{-1};
    std::int64_t benchmarkDelayedBaseline_{-1};
};

} // namespace wannaviewer

#endif
