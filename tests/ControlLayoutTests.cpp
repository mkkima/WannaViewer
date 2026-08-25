#include "TestHarness.hpp"
#include "wannaviewer/ui/ControlLayout.hpp"

#include <array>

namespace {

bool Inside(const wannaviewer::ui::Rect& inner, const wannaviewer::ui::Rect& outer) {
    return !inner.visible || (inner.x >= outer.x && inner.y >= outer.y &&
           inner.Right() <= outer.Right() && inner.Bottom() <= outer.Bottom());
}

bool Disjoint(const wannaviewer::ui::Rect& left, const wannaviewer::ui::Rect& right) {
    return !left.visible || !right.visible || left.Right() <= right.x || right.Right() <= left.x ||
           left.Bottom() <= right.y || right.Bottom() <= left.y;
}

} // namespace

WV_TEST("control layout remains bounded and non-overlapping across DPI modes") {
    for (const unsigned dpi : {96U, 120U, 144U, 192U}) {
        const int scale = static_cast<int>(dpi) / 24;
        const int width = 760 * scale / 4;
        const int height = 500 * scale / 4;
        const auto layout = wannaviewer::ui::ComputeControlLayout(width, height, dpi, true);
        const wannaviewer::ui::Rect client{0, 0, width, height, true};
        WV_REQUIRE(Inside(layout.frame, client));
        WV_REQUIRE(Inside(layout.video, layout.frame));
        WV_REQUIRE(Inside(layout.bar, layout.frame));
        const std::array controls{layout.timeline, layout.time, layout.play, layout.rewind, layout.forward,
                                  layout.mute, layout.volume,
                                  layout.audio, layout.subtitles, layout.quality, layout.shaders,
                                  layout.statistics, layout.fullscreen, layout.settings};
        for (const auto& control : controls) WV_REQUIRE(Inside(control, layout.bar));
        WV_REQUIRE(layout.timeline.width > 0);
        WV_REQUIRE(Inside(layout.emptyState, layout.video));
        WV_REQUIRE(Inside(layout.openFile, layout.emptyState));
        WV_REQUIRE(Inside(layout.openUrl, layout.emptyState));
        WV_REQUIRE(Disjoint(layout.timeline, layout.time));
        WV_REQUIRE(Disjoint(layout.play, layout.mute));
        WV_REQUIRE(Disjoint(layout.play, layout.rewind));
        WV_REQUIRE(Disjoint(layout.rewind, layout.forward));
        WV_REQUIRE(Disjoint(layout.forward, layout.mute));
        WV_REQUIRE(Disjoint(layout.mute, layout.volume));
        WV_REQUIRE(Disjoint(layout.volume, layout.audio));
        WV_REQUIRE(Disjoint(layout.audio, layout.subtitles));
        WV_REQUIRE(Disjoint(layout.subtitles, layout.quality));
        WV_REQUIRE(Disjoint(layout.quality, layout.shaders));
        WV_REQUIRE(Disjoint(layout.shaders, layout.statistics));
        WV_REQUIRE(Disjoint(layout.statistics, layout.fullscreen));
        WV_REQUIRE(Disjoint(layout.fullscreen, layout.settings));
    }
}

WV_TEST("hidden controls return the full client area to video") {
    const auto layout = wannaviewer::ui::ComputeControlLayout(1280, 720, 96, false);
    WV_REQUIRE(layout.frame.x == 0);
    WV_REQUIRE(layout.frame.y == 0);
    WV_REQUIRE(layout.frame.width == 1280);
    WV_REQUIRE(layout.frame.height == 720);
    WV_REQUIRE(layout.video.x == 0);
    WV_REQUIRE(layout.video.y == 0);
    WV_REQUIRE(layout.video.width == 1280);
    WV_REQUIRE(layout.video.height == 720);
    WV_REQUIRE(Inside(layout.video, layout.frame));
    WV_REQUIRE(!layout.bar.visible);
    WV_REQUIRE(!layout.play.visible);
    WV_REQUIRE(!layout.timeline.visible);
}

WV_TEST("embedded overlays remain bounded for choice input and message content") {
    for (const auto content : {wannaviewer::ui::OverlayContent::Choice,
                               wannaviewer::ui::OverlayContent::Input,
                               wannaviewer::ui::OverlayContent::Message}) {
        const auto layout = wannaviewer::ui::ComputeOverlayLayout(780, 500, 96, true, content);
        const wannaviewer::ui::Rect client{0, 0, 780, 500, true};
        WV_REQUIRE(Inside(layout.panel, client));
        for (const auto& control : {layout.title, layout.body, layout.edit, layout.list,
                                    layout.primary, layout.secondary})
            WV_REQUIRE(Inside(control, layout.panel));
        WV_REQUIRE(layout.primary.visible);
        WV_REQUIRE(layout.list.visible == (content == wannaviewer::ui::OverlayContent::Choice));
        WV_REQUIRE(layout.edit.visible == (content == wannaviewer::ui::OverlayContent::Input));
    }
}

WV_TEST("source selector stays inside the video area at supported sizes and DPI") {
    for (const unsigned dpi : {96U, 120U, 144U, 192U}) {
        const int width = 780 * static_cast<int>(dpi) / 96;
        const int contentHeight = 408 * static_cast<int>(dpi) / 96;
        const auto layout = wannaviewer::ui::ComputeSourceSelectorLayout(width, contentHeight, dpi, true);
        const wannaviewer::ui::Rect content{0, 0, width, contentHeight, true};
        WV_REQUIRE(Inside(layout.panel, content));
        const std::array controls{layout.title, layout.subtitle, layout.seasonLabel, layout.voiceLabel,
                                  layout.episodeLabel, layout.sourceLabel, layout.seasonList,
                                  layout.voiceList, layout.episodeList, layout.sourceList,
                                  layout.status, layout.open, layout.cancel};
        for (const auto& control : controls) WV_REQUIRE(Inside(control, layout.panel));
        WV_REQUIRE(layout.seasonList.width > 0);
        WV_REQUIRE(layout.seasonList.height > 0);
        WV_REQUIRE(Disjoint(layout.seasonList, layout.voiceList));
        WV_REQUIRE(Disjoint(layout.voiceList, layout.episodeList));
        WV_REQUIRE(Disjoint(layout.episodeList, layout.sourceList));
        WV_REQUIRE(Disjoint(layout.status, layout.open));
        WV_REQUIRE(Disjoint(layout.open, layout.cancel));
        WV_REQUIRE(layout.compact);
        WV_REQUIRE(layout.seasonList.width > width / 3);
    }
}

WV_TEST("wide source selector uses four columns in one row") {
    const auto layout = wannaviewer::ui::ComputeSourceSelectorLayout(1280, 760, 96, true);
    WV_REQUIRE(!layout.compact);
    WV_REQUIRE(layout.seasonList.y == layout.voiceList.y);
    WV_REQUIRE(layout.voiceList.y == layout.episodeList.y);
    WV_REQUIRE(layout.episodeList.y == layout.sourceList.y);
}

WV_TEST("hidden source selector hides every owned control") {
    const auto layout = wannaviewer::ui::ComputeSourceSelectorLayout(1280, 664, 96, false);
    const std::array controls{layout.panel, layout.title, layout.subtitle, layout.seasonLabel,
                              layout.voiceLabel, layout.episodeLabel, layout.sourceLabel,
                              layout.seasonList, layout.voiceList, layout.episodeList,
                              layout.sourceList, layout.status, layout.open, layout.cancel};
    for (const auto& control : controls) WV_REQUIRE(!control.visible);
}
