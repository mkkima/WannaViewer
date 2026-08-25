#include "wannaviewer/playback/MpvEngine.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wannaviewer {
namespace {

constexpr std::uint64_t kPauseObserver = 2;
constexpr std::uint64_t kTrackObserver = 3;
constexpr std::uint64_t kTimeObserver = 4;

std::string FormatMpvValue(const mpv_event_property& property) {
    if (!property.data) return {};
    switch (property.format) {
    case MPV_FORMAT_STRING:
        return *static_cast<char**>(property.data) ? *static_cast<char**>(property.data) : "";
    case MPV_FORMAT_FLAG:
        return *static_cast<int*>(property.data) ? "yes" : "no";
    case MPV_FORMAT_INT64:
        return std::to_string(*static_cast<std::int64_t*>(property.data));
    case MPV_FORMAT_DOUBLE:
        return std::format("{:.6f}", *static_cast<double*>(property.data));
    default:
        return {};
    }
}

std::int64_t InferBitDepth(std::string_view pixelFormat) {
    if (pixelFormat.find("p016") != std::string_view::npos || pixelFormat.find("16le") != std::string_view::npos ||
        pixelFormat.find("16be") != std::string_view::npos) return 16;
    if (pixelFormat.find("12le") != std::string_view::npos || pixelFormat.find("12be") != std::string_view::npos ||
        pixelFormat.find("p012") != std::string_view::npos) return 12;
    if (pixelFormat.find("10le") != std::string_view::npos || pixelFormat.find("10be") != std::string_view::npos ||
        pixelFormat.find("p010") != std::string_view::npos || pixelFormat.find("420p10") != std::string_view::npos) return 10;
    return pixelFormat.empty() ? 0 : 8;
}

} // namespace

MpvEngine::MpvEngine(const AppPaths& paths, const Config& config, Logger& logger)
    : paths_(paths), config_(config), logger_(logger) {}

MpvEngine::~MpvEngine() { Shutdown(); }

std::filesystem::path MpvEngine::LibraryPath() const {
#ifdef _WIN32
    return paths_.root / "libmpv-2.dll";
#else
    const auto bundled = paths_.root.parent_path() / "Frameworks" / "libmpv.2.dylib";
    return std::filesystem::exists(bundled) ? bundled : std::filesystem::path("libmpv.2.dylib");
#endif
}

void MpvEngine::SetRequiredOption(const char* name, const std::string& value) {
    const int result = api_.SetOptionString(handle_, name, value.c_str());
    if (result < 0) throw std::runtime_error(std::format("mpv option {}={} failed: {}", name, value, api_.ErrorString(result)));
}

void MpvEngine::Initialize(std::uintptr_t nativeWindow, EventCallback callback) {
    if (initialized_.exchange(true)) return;
    try {
        callback_ = std::move(callback);
        api_.Load(LibraryPath());
        handle_ = api_.Create();
        if (!handle_) throw std::runtime_error("mpv_create failed");

        SetRequiredOption("config", "no");
        SetRequiredOption("terminal", "no");
        SetRequiredOption("input-default-bindings", "no");
        SetRequiredOption("input-vo-keyboard", "no");
        SetRequiredOption("osc", "no");
        SetRequiredOption("idle", "yes");
        SetRequiredOption("keep-open", "yes");
        SetRequiredOption("background-color", "#090c11");
        SetRequiredOption("vo", "gpu-next");
        SetRequiredOption("hwdec", config_.GetString("playback.hwdec", "auto"));
        SetRequiredOption("interpolation", "no");
        SetRequiredOption("video-sync", config_.GetString("playback.video_sync", "display-resample"));
        SetRequiredOption("cache", "yes");
        // Start presenting as soon as demux/decode are ready. The normal cache
        // remains enabled and can still pause on a later underrun, but waiting
        // for a large initial network buffer makes healthy streams appear stuck
        // at 00:00.
        SetRequiredOption("cache-pause-initial", "no");
        const auto cacheMode = config_.GetString("network.cache_mode", "balanced");
        const auto readahead = cacheMode == "low-latency" ? "5" : cacheMode == "unstable" ? "60" :
                               config_.GetString("network.readahead_seconds", "20");
        const auto maximumCache = cacheMode == "low-latency" ? "67108864" : cacheMode == "unstable" ? "314572800" :
                                   config_.GetString("network.max_cache_bytes", "157286400");
        SetRequiredOption("demuxer-readahead-secs", readahead);
        SetRequiredOption("demuxer-max-bytes", maximumCache);
        SetRequiredOption("demuxer-max-back-bytes", "33554432");
        SetRequiredOption("gpu-shader-cache", "yes");
        SetRequiredOption("gpu-shader-cache-dir", paths_.cache.string());
        SetRequiredOption("target-colorspace-hint", "yes");
#ifdef _WIN32
        SetRequiredOption("gpu-api", "d3d11");
        SetRequiredOption("gpu-context", "d3d11");
        SetRequiredOption("d3d11-output-format", "auto");
        SetRequiredOption("d3d11-output-csp", "auto");
#endif
        std::int64_t window = static_cast<std::int64_t>(nativeWindow);
        const int windowResult = api_.SetOption(handle_, "wid", MPV_FORMAT_INT64, &window);
        if (windowResult < 0) throw std::runtime_error(std::string("Unable to set mpv window: ") + api_.ErrorString(windowResult));

        const int result = api_.Initialize(handle_);
        if (result < 0) throw std::runtime_error(std::string("mpv_initialize failed: ") + api_.ErrorString(result));

        (void)api_.RequestLogMessages(handle_, logger_.Level() >= LogLevel::Debug ? "info" : "warn");
        (void)api_.ObserveProperty(handle_, kPauseObserver, "pause", MPV_FORMAT_FLAG);
        (void)api_.ObserveProperty(handle_, kTrackObserver, "track-list/count", MPV_FORMAT_INT64);
        (void)api_.ObserveProperty(handle_, kTimeObserver, "time-pos", MPV_FORMAT_DOUBLE);
        eventThread_ = std::jthread([this](std::stop_token token) { EventLoop(token); });
#ifdef _WIN32
        logger_.Write(LogLevel::Info, "playback", "libmpv initialized: gpu-next, hwdec=auto, D3D11 output");
#else
        logger_.Write(LogLevel::Info, "playback", "libmpv initialized: gpu-next, hwdec=auto, Cocoa output");
#endif
    } catch (...) {
        initialized_ = false;
        if (handle_) {
            api_.TerminateDestroy(handle_);
            handle_ = nullptr;
        }
        throw;
    }
}

void MpvEngine::Shutdown() noexcept {
    if (!initialized_.exchange(false)) return;
    if (eventThread_.joinable()) {
        eventThread_.request_stop();
        api_.Wakeup(handle_);
        eventThread_.join();
    }
    api_.TerminateDestroy(handle_);
    handle_ = nullptr;
}

bool MpvEngine::IsInitialized() const noexcept { return initialized_.load(); }

void MpvEngine::Command(std::initializer_list<std::string> arguments) {
    if (!handle_) return;
    std::vector<const char*> pointers;
    pointers.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) pointers.push_back(argument.c_str());
    pointers.push_back(nullptr);
    const int result = api_.CommandAsync(handle_, nextRequestId_.fetch_add(1), pointers.data());
    if (result < 0) Emit({PlaybackEventType::Error, "command", api_.ErrorString(result)});
}

void MpvEngine::Open(std::string_view pathOrUrl, const std::vector<std::pair<std::string, std::string>>& headers,
                     std::string_view externalAudioUrl) {
    std::vector<std::string> fields;
    fields.reserve(std::min<std::size_t>(headers.size(), 64));
    std::size_t totalBytes = 0;
    for (const auto& [name, value] : headers) {
        if (fields.size() >= 64 || name.find_first_of("\r\n:") != std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos) continue;
        const auto fieldBytes = name.size() + value.size() + 2;
        if (fieldBytes > 16U * 1024U || totalBytes + fieldBytes > 64U * 1024U) continue;
        fields.push_back(name + ": " + value);
        totalBytes += fieldBytes;
    }
    std::vector<mpv_node> values(fields.size());
    for (std::size_t index = 0; index < fields.size(); ++index) {
        values[index].format = MPV_FORMAT_STRING;
        values[index].u.string = fields[index].data();
    }
    mpv_node_list list{static_cast<int>(values.size()), values.empty() ? nullptr : values.data(), nullptr};
    mpv_node headerArray{};
    headerArray.format = MPV_FORMAT_NODE_ARRAY;
    headerArray.u.list = &list;
    const int headerResult = api_.SetProperty(handle_, "http-header-fields", MPV_FORMAT_NODE, &headerArray);
    if (headerResult < 0)
        throw std::runtime_error(std::string("Unable to configure media request headers: ") +
                                 api_.ErrorString(headerResult));
    Command({"loadfile", std::string(pathOrUrl), "replace"});
    if (!externalAudioUrl.empty()) Command({"audio-add", std::string(externalAudioUrl), "select"});
}

void MpvEngine::Stop() { Command({"stop"}); }

void MpvEngine::TogglePause() { Command({"cycle", "pause"}); }
void MpvEngine::ToggleMute() { Command({"cycle", "mute"}); }
void MpvEngine::SeekRelative(double seconds) { Command({"seek", std::format("{:.3f}", seconds), "relative+exact"}); }
void MpvEngine::SeekAbsolute(double seconds) { Command({"seek", std::format("{:.3f}", seconds), "absolute+exact"}); }
void MpvEngine::FrameStep() { Command({"frame-step"}); }
void MpvEngine::ChangeChapter(int delta) { Command({"add", "chapter", std::to_string(delta)}); }
void MpvEngine::CycleAudio() { Command({"cycle", "audio"}); }
void MpvEngine::CycleSubtitles() { Command({"cycle", "sub"}); }
void MpvEngine::SetAudioTrack(std::int64_t id) { (void)api_.SetPropertyString(handle_, "aid", std::to_string(id).c_str()); }
void MpvEngine::SetSubtitleTrack(std::int64_t id) { (void)api_.SetPropertyString(handle_, "sid", id < 0 ? "no" : std::to_string(id).c_str()); }
void MpvEngine::SetVideoTrack(std::int64_t id) { (void)api_.SetPropertyString(handle_, "vid", std::to_string(id).c_str()); }
void MpvEngine::SetVolume(double value) {
    value = std::clamp(value, 0.0, 100.0);
    (void)api_.SetProperty(handle_, "volume", MPV_FORMAT_DOUBLE, &value);
}

void MpvEngine::AddSubtitle(std::string_view path) { Command({"sub-add", std::string(path), "select"}); }

void MpvEngine::ConfigureCache(std::string_view mode) {
    std::string readahead = "20";
    std::string maximum = "157286400";
    if (mode == "low-latency") { readahead = "5"; maximum = "67108864"; }
    else if (mode == "unstable") { readahead = "60"; maximum = "314572800"; }
    (void)api_.SetPropertyString(handle_, "demuxer-readahead-secs", readahead.c_str());
    (void)api_.SetPropertyString(handle_, "demuxer-max-bytes", maximum.c_str());
}

void MpvEngine::SetHardwareDecoding(bool enabled) {
    (void)api_.SetPropertyString(handle_, "hwdec", enabled ? "auto" : "no");
}

void MpvEngine::SetVideoSync(std::string_view mode) {
    const std::string value(mode);
    (void)api_.SetPropertyString(handle_, "video-sync", value.c_str());
}

void MpvEngine::SetShaders(const std::vector<std::filesystem::path>& shaders) {
    Command({"change-list", "glsl-shaders", "clr", ""});
    std::string display;
    for (const auto& shader : shaders) {
        Command({"change-list", "glsl-shaders", "append", shader.string()});
        if (!display.empty()) display += " + ";
        display += shader.filename().string();
    }
    std::scoped_lock lock(shaderMutex_);
    shaderChain_ = display.empty() ? "Off" : std::move(display);
}

std::vector<MediaTrack> MpvEngine::Tracks() const {
    std::scoped_lock lock(tracksMutex_);
    return tracks_;
}

double MpvEngine::Position() const { return handle_ ? api_.GetDouble(handle_, "time-pos") : 0.0; }
double MpvEngine::Duration() const { return handle_ ? api_.GetDouble(handle_, "duration") : 0.0; }
bool MpvEngine::IsPaused() const { return !handle_ || api_.GetFlag(handle_, "pause", true); }

PlaybackStatistics MpvEngine::Statistics() {
    PlaybackStatistics result;
    if (!handle_) return result;
    result.videoCodec = api_.GetString(handle_, "video-codec");
    const auto width = api_.GetInt64(handle_, "video-params/w");
    const auto height = api_.GetInt64(handle_, "video-params/h");
    if (width > 0 && height > 0) result.resolution = std::format("{}x{}", width, height);
    result.fps = api_.GetDouble(handle_, "estimated-vf-fps");
    result.videoBitrate = api_.GetInt64(handle_, "video-bitrate");
    result.audioBitrate = api_.GetInt64(handle_, "audio-bitrate");
    const auto decodedPixelFormat = api_.GetString(handle_, "video-dec-params/pixelformat");
    const auto surfaceFormat = api_.GetString(handle_, "video-out-params/hw-pixelformat");
    result.pixelFormat = decodedPixelFormat.empty() ? api_.GetString(handle_, "video-params/pixelformat") : decodedPixelFormat;
    if (!surfaceFormat.empty() && surfaceFormat != result.pixelFormat) result.pixelFormat += '/' + surfaceFormat;
    result.bitDepth = api_.GetInt64(handle_, "video-params/component-size");
    if (result.bitDepth <= 0) result.bitDepth = InferBitDepth(result.pixelFormat);
    result.colorPrimaries = api_.GetString(handle_, "video-dec-params/primaries");
    if (result.colorPrimaries.empty()) result.colorPrimaries = api_.GetString(handle_, "video-params/primaries");
    result.transferFunction = api_.GetString(handle_, "video-dec-params/gamma");
    if (result.transferFunction.empty()) result.transferFunction = api_.GetString(handle_, "video-params/gamma");
    result.hdrStatus = (result.transferFunction == "pq" || result.transferFunction == "hlg") ? "source HDR" : "SDR";
    result.hardwareDecoder = api_.GetString(handle_, "hwdec-current");
#ifdef _WIN32
    result.gpuRenderer = surfaceFormat.empty() ? "gpu-next/d3d11" : "gpu-next/d3d11 (" + surfaceFormat + ')';
#else
    result.gpuRenderer = surfaceFormat.empty() ? "gpu-next" : "gpu-next (" + surfaceFormat + ')';
#endif
    result.droppedFrames = api_.GetInt64(handle_, "frame-drop-count");
    result.delayedFrames = api_.GetInt64(handle_, "mistimed-frame-count");
    result.avSync = api_.GetDouble(handle_, "avsync");
    result.videoBufferSeconds = api_.GetDouble(handle_, "demuxer-cache-duration");
    result.networkCachePercent = api_.GetDouble(handle_, "cache-buffering-state");
    result.cpuPercent = cpuSampler_.Sample();
    {
        std::scoped_lock lock(shaderMutex_);
        result.shaderChain = shaderChain_;
    }
    return result;
}

void MpvEngine::RefreshTracks() {
    std::vector<MediaTrack> tracks;
    const auto count = api_.GetInt64(handle_, "track-list/count");
    for (std::int64_t index = 0; index < count && index < 512; ++index) {
        const auto prefix = std::format("track-list/{}/", index);
        MediaTrack track;
        track.id = api_.GetInt64(handle_, (prefix + "id").c_str());
        track.type = api_.GetString(handle_, (prefix + "type").c_str());
        track.title = api_.GetString(handle_, (prefix + "title").c_str());
        track.language = api_.GetString(handle_, (prefix + "lang").c_str());
        track.codec = api_.GetString(handle_, (prefix + "codec").c_str());
        track.width = api_.GetInt64(handle_, (prefix + "demux-w").c_str());
        track.height = api_.GetInt64(handle_, (prefix + "demux-h").c_str());
        track.selected = api_.GetFlag(handle_, (prefix + "selected").c_str());
        track.external = api_.GetFlag(handle_, (prefix + "external").c_str());
        tracks.push_back(std::move(track));
    }
    {
        std::scoped_lock lock(tracksMutex_);
        tracks_ = std::move(tracks);
    }
    Emit({PlaybackEventType::TracksChanged, {}, {}});
}

void MpvEngine::Emit(PlaybackEvent event) const {
    if (callback_) callback_(std::move(event));
}

void MpvEngine::EventLoop(std::stop_token stopToken) {
    bool playbackStarted = false;
    while (!stopToken.stop_requested()) {
        mpv_event* event = api_.WaitEvent(handle_, -1.0);
        if (stopToken.stop_requested()) break;
        if (!event) continue;
        switch (event->event_id) {
        case MPV_EVENT_START_FILE:
            playbackStarted = false;
            Emit({PlaybackEventType::StartFile, {}, {}});
            break;
        case MPV_EVENT_FILE_LOADED:
            RefreshTracks();
            Emit({PlaybackEventType::FileLoaded, {}, {}});
            break;
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end && end->reason == MPV_END_FILE_REASON_ERROR)
                Emit({PlaybackEventType::Error, "playback", api_.ErrorString(end->error)});
            Emit({PlaybackEventType::EndFile, {}, {}});
            break;
        }
        case MPV_EVENT_VIDEO_RECONFIG:
            Emit({PlaybackEventType::VideoReconfigured, {}, {}});
            break;
        case MPV_EVENT_PROPERTY_CHANGE: {
            const auto* property = static_cast<mpv_event_property*>(event->data);
            if (!property || !property->name) break;
            if (event->reply_userdata == kTrackObserver) RefreshTracks();
            if (event->reply_userdata == kTimeObserver && property->data &&
                *static_cast<double*>(property->data) > 0.01 && !playbackStarted) {
                playbackStarted = true;
                Emit({PlaybackEventType::PlaybackStarted, {}, {}});
            }
            Emit({PlaybackEventType::PropertyChanged, property->name, FormatMpvValue(*property)});
            break;
        }
        case MPV_EVENT_LOG_MESSAGE: {
            const auto* message = static_cast<mpv_event_log_message*>(event->data);
            if (!message || !message->text) break;
            const auto level = std::string_view(message->level ? message->level : "");
            logger_.Write(level == "error" || level == "fatal" ? LogLevel::Error : LogLevel::Debug,
                          message->prefix ? message->prefix : "mpv", message->text);
            const std::string text(message->text);
            if ((text.find("shader") != std::string::npos || text.find("Shader") != std::string::npos) &&
                (text.find("error") != std::string::npos || text.find("failed") != std::string::npos))
                Emit({PlaybackEventType::Error, "shader", text});
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            return;
        default:
            break;
        }
    }
}

} // namespace wannaviewer
