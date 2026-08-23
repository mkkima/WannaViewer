#include "wannaviewer/playback/PlaybackStatistics.hpp"

#include <algorithm>
#include <format>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <mach/mach.h>
#include <sys/time.h>
#endif

namespace wannaviewer {

std::string PlaybackStatistics::ToDisplayText() const {
    const auto bitrate = [](std::int64_t bits) { return static_cast<double>(bits) / 1'000'000.0; };
    return std::format(
        "Codec: {}\nResolution: {} @ {:.3f} fps\nVideo: {:.2f} Mbps | Audio: {:.3f} Mbps\n"
        "Pixel format: {} | {} bit\nPrimaries: {} | Transfer: {} | HDR: {}\n"
        "Decoder: {}\nRenderer: {}\nShaders: {}\n"
        "Dropped: {} | Delayed: {} | A/V sync: {:+.4f} s\n"
        "Buffer: {:.1f} s | Cache: {:.0f}%\nCPU: {:.1f}% | GPU frame timing: {}",
        videoCodec.empty() ? "N/A" : videoCodec, resolution.empty() ? "N/A" : resolution, fps,
        bitrate(videoBitrate), bitrate(audioBitrate), pixelFormat.empty() ? "N/A" : pixelFormat,
        bitDepth, colorPrimaries.empty() ? "N/A" : colorPrimaries,
        transferFunction.empty() ? "N/A" : transferFunction, hdrStatus,
        hardwareDecoder.empty() ? "software" : hardwareDecoder,
        gpuRenderer.empty() ? "gpu-next" : gpuRenderer, shaderChain,
        droppedFrames, delayedFrames, avSync, videoBufferSeconds, networkCachePercent, cpuPercent,
        gpuFrameMilliseconds > 0.0 ? std::format("{:.3f} ms", gpuFrameMilliseconds) : "not exposed by libmpv");
}

double ProcessCpuSampler::Sample() noexcept {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{}, wall{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0;
    GetSystemTimeAsFileTime(&wall);
    const auto toU64 = [](FILETIME value) {
        return (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U) | value.dwLowDateTime;
    };
    const auto process = toU64(kernel) + toU64(user);
    const auto currentWall = toU64(wall);
    if (previousWall100ns_ == 0) {
        previousProcess100ns_ = process;
        previousWall100ns_ = currentWall;
        return 0.0;
    }
    const auto processDelta = process - previousProcess100ns_;
    const auto wallDelta = currentWall - previousWall100ns_;
    previousProcess100ns_ = process;
    previousWall100ns_ = currentWall;
    if (wallDelta == 0) return 0.0;
    const auto processors = std::max(1U, std::thread::hardware_concurrency());
    return std::min(100.0, 100.0 * static_cast<double>(processDelta) /
                            static_cast<double>(wallDelta * processors));
#else
    return 0.0;
#endif
}

} // namespace wannaviewer
