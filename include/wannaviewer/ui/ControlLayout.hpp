#pragma once

namespace wannaviewer::ui {

struct Rect final {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    bool visible{true};

    [[nodiscard]] constexpr int Right() const noexcept { return x + width; }
    [[nodiscard]] constexpr int Bottom() const noexcept { return y + height; }
};

struct ControlLayout final {
    Rect frame;
    Rect video;
    Rect bar;
    Rect timeline;
    Rect time;
    Rect play;
    Rect rewind;
    Rect forward;
    Rect mute;
    Rect volume;
    Rect audio;
    Rect subtitles;
    Rect quality;
    Rect shaders;
    Rect statistics;
    Rect fullscreen;
    Rect settings;
    Rect emptyState;
    Rect openFile;
    Rect openUrl;
    bool compact{false};
};

enum class OverlayContent { Choice, Input, Message };

struct OverlayLayout final {
    Rect panel;
    Rect title;
    Rect body;
    Rect edit;
    Rect list;
    Rect primary;
    Rect secondary;
};

struct SourceSelectorLayout final {
    Rect panel;
    Rect title;
    Rect subtitle;
    Rect seasonLabel;
    Rect voiceLabel;
    Rect episodeLabel;
    Rect sourceLabel;
    Rect seasonList;
    Rect voiceList;
    Rect episodeList;
    Rect sourceList;
    Rect status;
    Rect open;
    Rect cancel;
    bool compact{false};
};

[[nodiscard]] ControlLayout ComputeControlLayout(int clientWidth, int clientHeight,
                                                  unsigned dpi, bool controlsVisible);
[[nodiscard]] SourceSelectorLayout ComputeSourceSelectorLayout(int clientWidth, int contentHeight,
                                                                unsigned dpi, bool visible);
[[nodiscard]] OverlayLayout ComputeOverlayLayout(int clientWidth, int clientHeight, unsigned dpi,
                                                  bool visible, OverlayContent content);

} // namespace wannaviewer::ui
