#pragma once

#ifdef _WIN32

#include "wannaviewer/core/AppPaths.hpp"
#include "wannaviewer/core/Config.hpp"
#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/network/HttpClient.hpp"
#include "wannaviewer/network/ResolverPipeline.hpp"
#include "wannaviewer/network/YtDlpBridge.hpp"
#include "wannaviewer/playback/MpvEngine.hpp"
#include "wannaviewer/playback/PlaybackStateStore.hpp"
#include "wannaviewer/rendering/ShaderManager.hpp"

#include <QWidget>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class QCloseEvent;
class QComboBox;
class QDragEnterEvent;
class QDropEvent;
class QFrame;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QMouseEvent;
class QProgressBar;
class QPropertyAnimation;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QSlider;
class QTimer;

namespace wannaviewer {

class MpvVideoWidget;
class UiTransition;

class QtPlayerWindow final : public QWidget {
public:
    QtPlayerWindow(AppPaths paths, Config config, Logger& logger,
                   bool backgroundTest = false, bool motionTest = false);
    ~QtPlayerWindow() override;

    QtPlayerWindow(const QtPlayerWindow&) = delete;
    QtPlayerWindow& operator=(const QtPlayerWindow&) = delete;

    void OpenInitial(std::string value);
    void EnableBenchmark(std::string value, std::string mode);

protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class OverlayMode { None, Choice, Url, Message };
    enum class OverlayAction { None, Audio, Subtitles, Video, Shaders, Settings };
    struct PendingMedia final {
        std::string value;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string externalAudioUrl;
    };

    void BuildUi();
    void ConnectUi();
    void ApplyTheme();
    void LayoutOverlays();
    void UpdateVisibility();
    void SetTransitionVisible(UiTransition& transition, bool visible);
    void StopUiAnimations();
    void ClearOverlayWidgets();
    void ClearSourceSelectorWidgets();
    void StartEngineInitialization();
    void HandleEngineInitialized();
    void OpenMedia(std::string value,
                   std::vector<std::pair<std::string, std::string>> headers,
                   std::string externalAudioUrl,
                   std::string resumeIdentity);
    void OpenFileDialog();
    void ShowUrlOverlay();
    void ResolveUrl(std::string value, HeaderMap inheritedHeaders = {}, bool preserveResumeIdentity = false);
    void HandleResolveResult(ResolveResult result);
    void ShowSourceSelector(ResolveResult result);
    void HideSourceSelector();
    void PopulateSourceSeasons();
    void PopulateSourceVoices();
    void PopulateSourceEpisodes();
    void PopulateSourceStreams();
    void UpdateSourceSelectionUi();
    [[nodiscard]] const StreamVariant* SelectedSource() const;
    [[nodiscard]] std::string SelectedSourceResumeIdentity() const;
    void OpenSelectedSource();
    void OpenVariant(const StreamVariant& stream);
    bool RetryBrowserProvider();
    void HandlePlaybackEvent(PlaybackEvent event);
    void UpdatePlaybackStartedState();
    void SetMediaLoaded(bool loaded);
    void UpdateUi();
    void UpdateTracks();
    void SeekFromSlider(bool commit);
    void TogglePlayback();
    void ToggleMute();
    void SeekRelative(double seconds);
    void CycleAudioTrack();
    void CycleSubtitleTrack();
    void ShowPlaybackFeedback(QString text);
    void HidePlaybackFeedback();
    void UpdateControlStates();
    void RestorePlaybackProgress();
    void SavePlaybackProgress(bool completed = false);
    void SaveUserSettings();
    void RecordInteraction();
    void ShowControls(bool show, bool animated = true);
    [[nodiscard]] bool CursorOverControls() const;
    void ToggleFullscreen();
    void ToggleStatistics();
    void ShowAudioMenu();
    void ShowSubtitleMenu();
    void ShowVideoMenu();
    void ShowShaderMenu();
    void ShowSettingsMenu();
    void ShowChoiceOverlay(QString title, QString hint, std::vector<QString> choices,
                           int selected, OverlayAction action);
    void ShowMessageOverlay(QString title, QString detail);
    void HideOverlay();
    void ApplyOverlaySelection();
    void ApplyShaderHotkey(int hotkey);
    void ApplyShaderPreset(std::size_t index);
    void OpenCustomShaders();
    void ShowError(QString title, std::string_view detail);
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
    PlaybackStateStore playbackState_;

    MpvVideoWidget* videoSurface_{nullptr};
    QFrame* controls_{nullptr};
    QSlider* timeline_{nullptr};
    QSlider* volume_{nullptr};
    QPushButton* playButton_{nullptr};
    QPushButton* rewindButton_{nullptr};
    QPushButton* forwardButton_{nullptr};
    QPushButton* muteButton_{nullptr};
    QPushButton* audioButton_{nullptr};
    QPushButton* subtitleButton_{nullptr};
    QPushButton* videoButton_{nullptr};
    QPushButton* shaderButton_{nullptr};
    QPushButton* statsButton_{nullptr};
    QPushButton* settingsButton_{nullptr};
    QPushButton* fullscreenButton_{nullptr};
    QLabel* timeLabel_{nullptr};
    QLabel* statistics_{nullptr};
    QLabel* playbackFeedback_{nullptr};
    QGraphicsOpacityEffect* controlsOpacity_{nullptr};
    QPropertyAnimation* controlsAnimation_{nullptr};
    QTimer* hideTimer_{nullptr};
    QTimer* feedbackTimer_{nullptr};
    QTimer* progressSaveTimer_{nullptr};
    QTimer* settingsSaveTimer_{nullptr};
    QTimer* uiTimer_{nullptr};

    QFrame* emptyState_{nullptr};
    QPushButton* emptyPlayIcon_{nullptr};
    QPushButton* openFileButton_{nullptr};
    QPushButton* openUrlButton_{nullptr};
    QFrame* openingState_{nullptr};
    QLabel* openingTitle_{nullptr};
    QLabel* openingStatus_{nullptr};
    QProgressBar* openingProgress_{nullptr};

    QFrame* modalScrim_{nullptr};
    QFrame* overlay_{nullptr};
    QLabel* overlayTitle_{nullptr};
    QLabel* overlayBody_{nullptr};
    QLineEdit* overlayEdit_{nullptr};
    QListWidget* overlayList_{nullptr};
    QPushButton* overlayPrimary_{nullptr};
    QPushButton* overlaySecondary_{nullptr};

    QFrame* sourcePanel_{nullptr};
    QLabel* sourceTitle_{nullptr};
    QLabel* sourceStatus_{nullptr};
    QComboBox* sourceSeason_{nullptr};
    QComboBox* sourceVoice_{nullptr};
    QComboBox* sourceEpisode_{nullptr};
    QComboBox* sourceStream_{nullptr};
    QPushButton* sourceOpen_{nullptr};
    QPushButton* sourceCancel_{nullptr};

    std::unique_ptr<UiTransition> emptyTransition_;
    std::unique_ptr<UiTransition> openingTransition_;
    std::unique_ptr<UiTransition> scrimTransition_;
    std::unique_ptr<UiTransition> overlayTransition_;
    std::unique_ptr<UiTransition> sourceTransition_;
    std::unique_ptr<UiTransition> statisticsTransition_;
    std::unique_ptr<UiTransition> feedbackTransition_;

    std::vector<std::int64_t> audioTrackIds_;
    std::vector<std::int64_t> subtitleTrackIds_;
    std::vector<std::int64_t> videoTrackIds_;
    std::vector<QString> audioTrackLabels_;
    std::vector<QString> subtitleTrackLabels_;
    std::vector<QString> videoTrackLabels_;
    std::jthread resolverThread_;
    std::jthread engineInitializationThread_;
    std::mutex engineInitializationMutex_;
    std::optional<std::string> engineInitializationError_;
    std::optional<PendingMedia> pendingMedia_;
    std::atomic_bool closing_{false};
    std::atomic_bool resolving_{false};
    std::optional<ResolveResult> sourceSelection_;
    OverlayMode overlayMode_{OverlayMode::None};
    OverlayAction overlayAction_{OverlayAction::None};
    std::vector<QString> overlayChoices_;
    int sourceSeasonIndex_{-1};
    int sourceVoiceIndex_{-1};
    int sourceEpisodeIndex_{-1};
    int sourceStreamIndex_{-1};
    int audioSelection_{-1};
    int subtitleSelection_{0};
    int videoSelection_{-1};
    std::size_t shaderPresetIndex_{0};
    bool engineReady_{false};
    bool mediaOpening_{false};
    bool mediaLoaded_{false};
    bool playbackClockAdvanced_{false};
    bool playbackStarted_{false};
    bool startupTimeoutReported_{false};
    bool controlsVisible_{true};
    bool statisticsVisible_{false};
    bool feedbackVisible_{false};
    bool playbackStateAvailable_{true};
    bool resumeSaveSuspended_{false};
    bool settingsDirty_{false};
    bool timelineDragging_{false};
    std::optional<int> pendingTimelineValue_;
    std::chrono::steady_clock::time_point pendingTimelineStarted_{};
    bool fullscreen_{false};
    bool backgroundTest_{false};
    bool motionEnabled_{true};
    bool motionTest_{false};
    bool benchmarkMode_{false};
    bool benchmarkRunning_{false};
    std::optional<bool> displayedPlaying_;
    std::optional<bool> displayedMuted_;
    std::string benchmarkInput_;
    std::string benchmarkProfile_{"hardware"};
    std::string browserRetryUrl_;
    std::string pendingResumeIdentity_;
    std::string pendingResumeKey_;
    std::string currentResumeKey_;
    HeaderMap browserRetryHeaders_;
    unsigned browserRetriesRemaining_{0};
    std::chrono::steady_clock::time_point playbackLoadStarted_{};
    std::chrono::seconds playbackStartupTimeout_{30};
    std::chrono::steady_clock::time_point lastStatisticsUpdate_{};
    std::chrono::steady_clock::time_point lastBenchmarkSample_{};
    std::chrono::steady_clock::time_point benchmarkStart_{};
    std::vector<PlaybackStatistics> benchmarkSamples_;
    std::int64_t benchmarkDroppedBaseline_{-1};
    std::int64_t benchmarkDelayedBaseline_{-1};
};

} // namespace wannaviewer

#endif
