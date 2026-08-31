#pragma once

#include "wannaviewer/core/AppPaths.hpp"
#include "wannaviewer/core/Config.hpp"
#include "wannaviewer/core/Logger.hpp"
#include "wannaviewer/playback/MpvApi.hpp"
#include "wannaviewer/playback/PlaybackStatistics.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

namespace wannaviewer {

enum class PlaybackEventType {
    StartFile,
    FileLoaded,
    PlaybackStarted,
    EndFile,
    TracksChanged,
    VideoReconfigured,
    PropertyChanged,
    Error
};

struct PlaybackEvent final {
    PlaybackEventType type{PlaybackEventType::PropertyChanged};
    std::string name;
    std::string value;
};

struct MediaTrack final {
    std::int64_t id{0};
    std::string type;
    std::string title;
    std::string language;
    std::string codec;
    std::int64_t width{0};
    std::int64_t height{0};
    bool selected{false};
    bool external{false};
};

class MpvEngine final {
public:
    using EventCallback = std::function<void(PlaybackEvent)>;

    MpvEngine(const AppPaths& paths, const Config& config, Logger& logger);
    ~MpvEngine();
    MpvEngine(const MpvEngine&) = delete;
    MpvEngine& operator=(const MpvEngine&) = delete;

    void Initialize(std::uintptr_t nativeWindow, EventCallback callback);
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] bool UsesOpenGlRenderApi() const noexcept;
    void CreateOpenGlRenderContext(mpv_opengl_init_params* initialization,
                                   mpv_render_update_fn updateCallback, void* callbackContext);
    void DestroyOpenGlRenderContext() noexcept;
    [[nodiscard]] bool RenderOpenGl(int frameBuffer, int width, int height);

    void Open(std::string_view pathOrUrl, const std::vector<std::pair<std::string, std::string>>& headers = {},
              std::string_view externalAudioUrl = {});
    void Stop();
    void SetPaused(bool paused);
    void TogglePause();
    void ToggleMute();
    void SeekRelative(double seconds);
    void SeekAbsolute(double seconds);
    void FrameStep();
    void ChangeChapter(int delta);
    void CycleAudio();
    void CycleSubtitles();
    void SetAudioTrack(std::int64_t id);
    void SetSubtitleTrack(std::int64_t id);
    void SetVideoTrack(std::int64_t id);
    void SetVolume(double value);
    void AddSubtitle(std::string_view path);
    void ConfigureCache(std::string_view mode);
    void SetHardwareDecoding(bool enabled);
    void SetVideoSync(std::string_view mode);
    void SetShaders(const std::vector<std::filesystem::path>& shaders);

    [[nodiscard]] std::vector<MediaTrack> Tracks() const;
    [[nodiscard]] PlaybackStatistics Statistics();
    [[nodiscard]] double Position() const;
    [[nodiscard]] double Duration() const;
    [[nodiscard]] bool IsPaused() const;
    [[nodiscard]] bool IsMuted() const;

private:
    void SetRequiredOption(const char* name, const std::string& value);
    void Command(std::initializer_list<std::string> arguments);
    void EventLoop(std::stop_token stopToken);
    void Emit(PlaybackEvent event) const;
    void RefreshTracks();
    [[nodiscard]] std::filesystem::path LibraryPath() const;

    const AppPaths& paths_;
    const Config& config_;
    Logger& logger_;
    MpvApi api_;
    mpv_handle* handle_{nullptr};
    EventCallback callback_;
    std::jthread eventThread_;
    std::atomic_bool initialized_{false};
    std::atomic_bool initializing_{false};
    bool openGlRenderApi_{false};
    mpv_render_context* renderContext_{nullptr};
    mutable std::mutex tracksMutex_;
    std::vector<MediaTrack> tracks_;
    mutable std::mutex shaderMutex_;
    std::string shaderChain_{"Off"};
    ProcessCpuSampler cpuSampler_;
    std::atomic_uint64_t nextRequestId_{1};
};

} // namespace wannaviewer
