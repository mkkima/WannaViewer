#include "wannaviewer/ui/ControlLayout.hpp"

#include <algorithm>
#include <array>

namespace wannaviewer::ui {
namespace {

int Scaled(int value, unsigned dpi) {
    return std::max(1, static_cast<int>((static_cast<long long>(value) * std::max(1U, dpi) + 48LL) / 96LL));
}

Rect TakeFromRight(int& right, int y, int width, int height, int gap, bool visible) {
    if (!visible) return {right, y, 0, height, false};
    right -= width;
    const Rect result{right, y, width, height, true};
    right -= gap;
    return result;
}

} // namespace

ControlLayout ComputeControlLayout(int clientWidth, int clientHeight, unsigned dpi, bool controlsVisible) {
    ControlLayout result;
    clientWidth = std::max(0, clientWidth);
    clientHeight = std::max(0, clientHeight);
    const auto s = [dpi](int value) { return Scaled(value, dpi); };
    result.compact = clientWidth < s(1040);

    result.frame = {0, 0, clientWidth, clientHeight, true};
    result.video = result.frame;
    const int barHeight = controlsVisible ? s(result.compact ? 82 : 88) : 0;
    const int controlsTop = std::max(result.frame.y, result.frame.Bottom() - barHeight);
    result.bar = {result.frame.x, controlsTop, result.frame.width, barHeight, controlsVisible};

    const int padding = s(result.compact ? 12 : 16);
    const int gap = s(result.compact ? 3 : 5);
    result.timeline = {result.bar.x + padding, result.bar.y,
                       std::max(0, result.bar.width - padding * 2), s(34), controlsVisible};

    const int rowY = result.bar.y + s(result.compact ? 36 : 40);
    const int buttonSize = s(result.compact ? 34 : 38);
    int left = result.bar.x + padding;
    const auto takeLeft = [&]() {
        const Rect rectangle{left, rowY, buttonSize, buttonSize, controlsVisible};
        left = rectangle.Right() + gap;
        return rectangle;
    };
    result.play = takeLeft();
    result.rewind = takeLeft();
    result.forward = takeLeft();
    result.mute = takeLeft();
    const bool showVolume = controlsVisible && !result.compact;
    result.volume = {left + s(2), rowY + s(7), showVolume ? s(72) : 0, s(22), showVolume};
    if (showVolume) left = result.volume.Right() + gap;
    const int timeWidth = s(result.compact ? 100 : 122);
    result.time = {left + s(2), rowY, timeWidth, buttonSize, controlsVisible};

    int right = result.bar.Right() - padding;
    result.fullscreen = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);
    result.settings = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);
    result.statistics = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);
    result.shaders = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);
    result.quality = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);
    result.subtitles = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);
    result.audio = TakeFromRight(right, rowY, buttonSize, buttonSize, gap, controlsVisible);

    const int emptyWidth = std::max(0, std::min(s(500), result.video.width - s(40)));
    const int emptyHeight = std::max(0, std::min(s(250), result.video.height - s(40)));
    result.emptyState = {result.video.x + (result.video.width - emptyWidth) / 2,
                         result.video.y + (result.video.height - emptyHeight) / 2,
                         emptyWidth, emptyHeight, true};
    const int actionWidth = std::min(s(136), std::max(s(100), (emptyWidth - s(44)) / 2));
    const int actionGap = s(12);
    const int actionsWidth = actionWidth * 2 + actionGap;
    const int actionY = result.emptyState.y + result.emptyState.height - s(58);
    const int actionX = result.emptyState.x + (result.emptyState.width - actionsWidth) / 2;
    result.openFile = {actionX, actionY, actionWidth, s(36), true};
    result.openUrl = {actionX + actionWidth + actionGap, actionY, actionWidth, s(36), true};
    return result;
}

int TimelineValueFromPoint(int x, int channelLeft, int channelRight,
                           int minimum, int maximum) noexcept {
    if (maximum <= minimum || channelRight <= channelLeft) return minimum;
    const int clamped = std::clamp(x, channelLeft, channelRight) - channelLeft;
    const long long width = static_cast<long long>(channelRight - channelLeft);
    const long long range = static_cast<long long>(maximum) - minimum;
    return minimum + static_cast<int>((range * clamped + width / 2) / width);
}

SourceSelectorLayout ComputeSourceSelectorLayout(int clientWidth, int contentHeight,
                                                  unsigned dpi, bool visible) {
    SourceSelectorLayout result;
    clientWidth = std::max(0, clientWidth);
    contentHeight = std::max(0, contentHeight);
    const auto s = [dpi](int value) { return Scaled(value, dpi); };
    const int outerMargin = s(16);
    const int panelWidth = std::max(0, std::min(s(1120), clientWidth - outerMargin * 2));
    const int panelHeight = std::max(0, std::min(s(500), contentHeight - outerMargin * 2));
    result.panel = {(clientWidth - panelWidth) / 2, (contentHeight - panelHeight) / 2,
                    panelWidth, panelHeight, visible};

    const int padding = s(20);
    const int gap = s(12);
    const int innerLeft = result.panel.x + padding;
    const int innerRight = result.panel.Right() - padding;
    const int innerWidth = std::max(0, innerRight - innerLeft);
    result.compact = clientWidth < s(960);
    result.title = {innerLeft, result.panel.y + s(16), innerWidth, s(32), visible};
    result.subtitle = {innerLeft, result.panel.y + s(48), innerWidth, s(24), visible};

    const int labelsY = result.panel.y + s(82);
    const int listsY = labelsY + s(24);
    const int buttonHeight = s(36);
    const int footerY = result.panel.Bottom() - padding - buttonHeight;
    const int rawListHeight = std::max(0, footerY - s(14) - listsY);
    const int rowHeight = s(result.compact ? 30 : 34);
    if (result.compact) {
        const int columnWidth = std::max(0, (innerWidth - gap) / 2);
        const int lastWidth = std::max(0, innerWidth - columnWidth - gap);
        const int availableForLists = std::max(0, rawListHeight - gap - s(22));
        const int rawRowHeight = availableForLists / 2;
        const int listHeight = rowHeight > 0 ? rawRowHeight - rawRowHeight % rowHeight : rawRowHeight;
        const int secondLabelsY = listsY + listHeight + gap;
        const int secondListsY = secondLabelsY + s(22);
        result.seasonLabel = {innerLeft, labelsY, columnWidth, s(22), visible};
        result.voiceLabel = {innerLeft + columnWidth + gap, labelsY, lastWidth, s(22), visible};
        result.episodeLabel = {innerLeft, secondLabelsY, columnWidth, s(22), visible};
        result.sourceLabel = {innerLeft + columnWidth + gap, secondLabelsY, lastWidth, s(22), visible};
        result.seasonList = {innerLeft, listsY, columnWidth, listHeight, visible};
        result.voiceList = {innerLeft + columnWidth + gap, listsY, lastWidth, listHeight, visible};
        result.episodeList = {innerLeft, secondListsY, columnWidth, listHeight, visible};
        result.sourceList = {innerLeft + columnWidth + gap, secondListsY, lastWidth, listHeight, visible};
    } else {
        const int listHeight = rowHeight > 0 ? rawListHeight - rawListHeight % rowHeight : rawListHeight;
        const int columnWidth = std::max(0, (innerWidth - gap * 3) / 4);
        const int lastWidth = std::max(0, innerWidth - columnWidth * 3 - gap * 3);
        const std::array<int, 4> x{innerLeft, innerLeft + columnWidth + gap,
                                   innerLeft + (columnWidth + gap) * 2,
                                   innerLeft + (columnWidth + gap) * 3};
        const std::array<int, 4> widths{columnWidth, columnWidth, columnWidth, lastWidth};
        result.seasonLabel = {x[0], labelsY, widths[0], s(22), visible};
        result.voiceLabel = {x[1], labelsY, widths[1], s(22), visible};
        result.episodeLabel = {x[2], labelsY, widths[2], s(22), visible};
        result.sourceLabel = {x[3], labelsY, widths[3], s(22), visible};
        result.seasonList = {x[0], listsY, widths[0], listHeight, visible};
        result.voiceList = {x[1], listsY, widths[1], listHeight, visible};
        result.episodeList = {x[2], listsY, widths[2], listHeight, visible};
        result.sourceList = {x[3], listsY, widths[3], listHeight, visible};
    }

    const int openWidth = s(124);
    const int cancelWidth = s(96);
    result.cancel = {innerRight - cancelWidth, footerY, cancelWidth, buttonHeight, visible};
    result.open = {result.cancel.x - gap - openWidth, footerY, openWidth, buttonHeight, visible};
    result.status = {innerLeft, footerY, std::max(0, result.open.x - gap - innerLeft),
                     buttonHeight, visible};
    return result;
}

OverlayLayout ComputeOverlayLayout(int clientWidth, int clientHeight, unsigned dpi,
                                   bool visible, OverlayContent content) {
    OverlayLayout result;
    clientWidth = std::max(0, clientWidth);
    clientHeight = std::max(0, clientHeight);
    const auto s = [dpi](int value) { return Scaled(value, dpi); };
    const int margin = s(20);
    const int desiredWidth = content == OverlayContent::Choice ? s(500) : s(680);
    const int desiredHeight = content == OverlayContent::Choice ? s(500)
                            : content == OverlayContent::Input ? s(220) : s(250);
    const int width = std::max(0, std::min(desiredWidth, clientWidth - margin * 2));
    const int height = std::max(0, std::min(desiredHeight, clientHeight - margin * 2));
    result.panel = {(clientWidth - width) / 2, (clientHeight - height) / 2, width, height, visible};
    const int padding = s(24);
    const int innerLeft = result.panel.x + padding;
    const int innerWidth = std::max(0, result.panel.width - padding * 2);
    result.title = {innerLeft, result.panel.y + s(18), innerWidth, s(34), visible};
    result.body = {innerLeft, result.panel.y + s(58), innerWidth,
                   content == OverlayContent::Message ? s(100) : s(28), visible};
    const int buttonHeight = s(38);
    const int footerY = result.panel.Bottom() - padding - buttonHeight;
    const int gap = s(10);
    const int primaryWidth = s(120);
    const int secondaryWidth = s(100);
    const bool secondaryVisible = visible && content != OverlayContent::Message;
    result.secondary = {result.panel.Right() - padding - secondaryWidth, footerY,
                        secondaryWidth, buttonHeight, secondaryVisible};
    result.primary = {secondaryVisible ? result.secondary.x - gap - primaryWidth
                                       : result.panel.Right() - padding - primaryWidth,
                      footerY, primaryWidth, buttonHeight, visible};
    result.edit = {innerLeft, result.panel.y + s(98), innerWidth, s(42),
                   visible && content == OverlayContent::Input};
    result.list = {innerLeft, result.panel.y + s(94), innerWidth,
                   std::max(0, footerY - s(14) - (result.panel.y + s(94))),
                   visible && content == OverlayContent::Choice};
    return result;
}

} // namespace wannaviewer::ui
