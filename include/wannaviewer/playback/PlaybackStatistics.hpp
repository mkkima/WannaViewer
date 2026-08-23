#pragma once

#include <cstdint>
#include <string>

namespace wannaviewer {

struct PlaybackStatistics final {
    std::string videoCodec;
    std::string resolution;
    double fps{0.0};
    std::int64_t videoBitrate{0};
    std::int64_t audioBitrate{0};
    std::string pixelFormat;
    std::int64_t bitDepth{0};
    std::string colorPrimaries;
    std::string transferFunction;
    std::string hdrStatus;
    std::string hardwareDecoder;
    std::string gpuRenderer;
    std::string shaderChain;
    std::int64_t droppedFrames{0};
    std::int64_t delayedFrames{0};
    double avSync{0.0};
    double videoBufferSeconds{0.0};
    double networkCachePercent{0.0};
    double cpuPercent{0.0};
    double gpuFrameMilliseconds{0.0};

    [[nodiscard]] std::string ToDisplayText() const;
};

class ProcessCpuSampler final {
public:
    [[nodiscard]] double Sample() noexcept;

private:
    std::uint64_t previousProcess100ns_{0};
    std::uint64_t previousWall100ns_{0};
};

} // namespace wannaviewer
