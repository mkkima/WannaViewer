#include "wannaviewer/platform/windows/PlayerWindow.hpp"

#include "wannaviewer/resolvers/DirectMediaResolver.hpp"
#include "wannaviewer/resolvers/AnimeGoResolver.hpp"
#include "wannaviewer/resolvers/AniBoomResolver.hpp"
#include "wannaviewer/resolvers/BrowserEmbedResolver.hpp"
#include "wannaviewer/resolvers/CdnVideoHubResolver.hpp"
#include "wannaviewer/resolvers/GenericResolver.hpp"
#include "wannaviewer/resolvers/YummyAnimeResolver.hpp"
#include "wannaviewer/ui/ControlLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <format>
#include <fstream>
#include <memory>
#include <numeric>
#include <stdexcept>

#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <nlohmann/json.hpp>

namespace wannaviewer {
namespace {

constexpr UINT kPlaybackMessage = WM_APP + 1;
constexpr UINT kResolverMessage = WM_APP + 2;
constexpr UINT kInteractionMessage = WM_APP + 3;
constexpr UINT kTimelineHoverMessage = WM_APP + 4;
constexpr UINT kTimelineSeekMessage = WM_APP + 5;
constexpr UINT_PTR kUiTimer = 1;
constexpr ULONGLONG kControlsHideDelayMs = 1500;
constexpr ULONGLONG kControlsShowAnimationMs = 160;
constexpr ULONGLONG kControlsHideAnimationMs = 120;
constexpr ULONGLONG kTimelineAnimationMs = 120;
constexpr ULONGLONG kTimelineSeekPreviewMs = 1500;
constexpr UINT kControlsAnimationTimerMs = 16;
constexpr UINT kUiRefreshTimerMs = 33;
constexpr int kTimelineChannelInset = 7;
constexpr wchar_t kHoverProperty[] = L"WannaViewer.Hovered";
constexpr wchar_t kTimelineDragProperty[] = L"WannaViewer.TimelineDragging";

enum class TimelineInput : WPARAM { Begin, Update, Commit, Cancel };
constexpr int kPlay = 100;
constexpr int kTimeline = 101;
constexpr int kVolume = 102;
constexpr int kAudio = 103;
constexpr int kSubtitles = 104;
constexpr int kShader = 105;
constexpr int kFullscreen = 106;
constexpr int kSettings = 107;
constexpr int kVideoQuality = 108;
constexpr int kMute = 109;
constexpr int kStatistics = 110;
constexpr int kOpenFile = 111;
constexpr int kOpenUrl = 112;
constexpr int kControlsBar = 113;
constexpr int kEmptyState = 114;
constexpr int kRewind = 116;
constexpr int kForward = 117;
constexpr int kSourcePanel = 130;
constexpr int kSourceTitle = 131;
constexpr int kSourceSubtitle = 132;
constexpr int kSourceSeasonLabel = 133;
constexpr int kSourceVoiceLabel = 134;
constexpr int kSourceEpisodeLabel = 135;
constexpr int kSourceStreamLabel = 136;
constexpr int kSourceSeasonList = 137;
constexpr int kSourceVoiceList = 138;
constexpr int kSourceEpisodeList = 139;
constexpr int kSourceStreamList = 140;
constexpr int kSourceStatus = 141;
constexpr int kSourceOpen = 142;
constexpr int kSourceCancel = 143;
constexpr int kOverlayPanel = 150;
constexpr int kOverlayTitle = 151;
constexpr int kOverlayBody = 152;
constexpr int kOverlayEdit = 153;
constexpr int kOverlayList = 154;
constexpr int kOverlayPrimary = 155;
constexpr int kOverlaySecondary = 156;

constexpr COLORREF kWindowColor = RGB(0, 0, 0);
constexpr COLORREF kPanelColor = RGB(3, 3, 3);
constexpr COLORREF kStatisticsColor = RGB(8, 8, 8);
constexpr COLORREF kButtonHotColor = RGB(28, 28, 28);
constexpr COLORREF kButtonPressedColor = RGB(42, 42, 42);
constexpr COLORREF kTextColor = RGB(238, 238, 238);
constexpr COLORREF kMutedTextColor = RGB(164, 164, 164);
constexpr COLORREF kBorderColor = RGB(55, 55, 55);
constexpr auto kDwmWindowCornerPreference = static_cast<DWMWINDOWATTRIBUTE>(33);
constexpr int kDwmRoundCorners = 2;

Gdiplus::Color SmoothColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

COLORREF BlendColor(COLORREF from, COLORREF to, double amount) {
    amount = std::clamp(amount, 0.0, 1.0);
    const auto blend = [amount](BYTE start, BYTE end) {
        return static_cast<BYTE>(std::lround(static_cast<double>(start) +
                                             (static_cast<double>(end) - start) * amount));
    };
    return RGB(blend(GetRValue(from), GetRValue(to)),
               blend(GetGValue(from), GetGValue(to)),
               blend(GetBValue(from), GetBValue(to)));
}

void ConfigureSmoothGraphics(Gdiplus::Graphics& graphics) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}

void AddRoundedRectangle(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rectangle, float diameter) {
    diameter = std::clamp(diameter, 1.0F, std::min(rectangle.Width, rectangle.Height));
    path.AddArc(rectangle.X, rectangle.Y, diameter, diameter, 180.0F, 90.0F);
    path.AddArc(rectangle.GetRight() - diameter, rectangle.Y, diameter, diameter, 270.0F, 90.0F);
    path.AddArc(rectangle.GetRight() - diameter, rectangle.GetBottom() - diameter,
                diameter, diameter, 0.0F, 90.0F);
    path.AddArc(rectangle.X, rectangle.GetBottom() - diameter, diameter, diameter, 90.0F, 90.0F);
    path.CloseFigure();
}

void FillRoundedRectangle(HDC dc, RECT rectangle, COLORREF color, int diameter) {
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (!dc || width <= 0 || height <= 0) return;
    Gdiplus::Graphics graphics(dc);
    ConfigureSmoothGraphics(graphics);
    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path,
        Gdiplus::RectF(static_cast<float>(rectangle.left), static_cast<float>(rectangle.top),
                       static_cast<float>(width), static_cast<float>(height)),
        static_cast<float>(diameter));
    Gdiplus::SolidBrush fill(SmoothColor(color));
    graphics.FillPath(&fill, &path);
}

void PaintRoundedRectangle(HDC dc, RECT rectangle, COLORREF fillColor, COLORREF borderColor,
                           int diameter, int penWidth = 1) {
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (!dc || width <= 0 || height <= 0) return;
    const float inset = static_cast<float>(std::max(1, penWidth)) / 2.0F;
    Gdiplus::Graphics graphics(dc);
    ConfigureSmoothGraphics(graphics);
    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path,
        Gdiplus::RectF(static_cast<float>(rectangle.left) + inset,
                       static_cast<float>(rectangle.top) + inset,
                       std::max(1.0F, static_cast<float>(width) - inset * 2.0F),
                       std::max(1.0F, static_cast<float>(height) - inset * 2.0F)),
        static_cast<float>(diameter));
    Gdiplus::SolidBrush fill(SmoothColor(fillColor));
    Gdiplus::Pen border(SmoothColor(borderColor), static_cast<float>(std::max(1, penWidth)));
    border.SetAlignment(Gdiplus::PenAlignmentInset);
    graphics.FillPath(&fill, &path);
    graphics.DrawPath(&border, &path);
}

void FillSmoothEllipse(HDC dc, RECT rectangle, COLORREF color) {
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (!dc || width <= 0 || height <= 0) return;
    Gdiplus::Graphics graphics(dc);
    ConfigureSmoothGraphics(graphics);
    Gdiplus::SolidBrush fill(SmoothColor(color));
    graphics.FillEllipse(&fill, static_cast<float>(rectangle.left), static_cast<float>(rectangle.top),
                         static_cast<float>(width), static_cast<float>(height));
}

void FillSmoothEllipse(HDC dc, float left, float top, float width, float height, COLORREF color) {
    if (!dc || width <= 0.0F || height <= 0.0F) return;
    Gdiplus::Graphics graphics(dc);
    ConfigureSmoothGraphics(graphics);
    Gdiplus::SolidBrush fill(SmoothColor(color));
    graphics.FillEllipse(&fill, left, top, width, height);
}

void StrokeRoundedRectangle(HDC dc, RECT rectangle, COLORREF color, int diameter,
                            int penWidth = 1) {
    const int width = rectangle.right - rectangle.left;
    const int height = rectangle.bottom - rectangle.top;
    if (!dc || width <= 0 || height <= 0) return;
    const float inset = static_cast<float>(std::max(1, penWidth)) / 2.0F;
    Gdiplus::Graphics graphics(dc);
    ConfigureSmoothGraphics(graphics);
    Gdiplus::GraphicsPath path;
    AddRoundedRectangle(path,
        Gdiplus::RectF(static_cast<float>(rectangle.left) + inset,
                       static_cast<float>(rectangle.top) + inset,
                       std::max(1.0F, static_cast<float>(width) - inset * 2.0F),
                       std::max(1.0F, static_cast<float>(height) - inset * 2.0F)),
        static_cast<float>(diameter));
    Gdiplus::Pen pen(SmoothColor(color), static_cast<float>(std::max(1, penWidth)));
    pen.SetAlignment(Gdiplus::PenAlignmentInset);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawPath(&pen, &path);
}

std::optional<RECT> RelativeControlRectangle(HWND control, HWND surface, int grow) {
    if (!control || !surface || !IsWindowVisible(control)) return std::nullopt;
    RECT rectangle{};
    if (!GetWindowRect(control, &rectangle)) return std::nullopt;
    MapWindowPoints(HWND_DESKTOP, surface, reinterpret_cast<POINT*>(&rectangle), 2);
    InflateRect(&rectangle, grow, grow);
    return rectangle;
}

struct WindowPlacement final {
    HWND control{nullptr};
    ui::Rect rectangle;
};

struct PendingWindowPlacement final {
    HWND control{nullptr};
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    UINT flags{0};
};

constexpr std::size_t kMaxWindowPlacements = 64;

void ApplyWindowLayout(HWND parent,
                       const std::array<WindowPlacement, kMaxWindowPlacements>& placements,
                       std::size_t placementCount) {
    std::array<PendingWindowPlacement, kMaxWindowPlacements> pending{};
    std::size_t pendingCount = 0;
    for (std::size_t index = 0; index < placementCount; ++index) {
        const auto& placement = placements[index];
        if (!placement.control) continue;
        const bool currentlyVisible =
            (GetWindowLongPtrW(placement.control, GWL_STYLE) & WS_VISIBLE) != 0;
        if (!placement.rectangle.visible) {
            if (currentlyVisible) {
                pending[pendingCount++] = {placement.control, 0, 0, 0, 0,
                    SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER |
                    SWP_NOMOVE | SWP_NOSIZE | SWP_HIDEWINDOW};
            }
            continue;
        }

        RECT current{};
        const bool hasRectangle = GetWindowRect(placement.control, &current) != FALSE;
        if (hasRectangle)
            MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&current), 2);
        const bool geometryChanged = !hasRectangle ||
            current.left != placement.rectangle.x || current.top != placement.rectangle.y ||
            current.right - current.left != placement.rectangle.width ||
            current.bottom - current.top != placement.rectangle.height;
        if (!geometryChanged && currentlyVisible) continue;

        UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER;
        if (!geometryChanged)
            flags |= SWP_NOMOVE | SWP_NOSIZE;
        else
            flags |= SWP_NOCOPYBITS;
        if (!currentlyVisible) flags |= SWP_SHOWWINDOW;
        pending[pendingCount++] = {placement.control, placement.rectangle.x, placement.rectangle.y,
                                   placement.rectangle.width, placement.rectangle.height, flags};
    }
    if (pendingCount == 0) return;

    const auto applyIndividually = [&pending, pendingCount] {
        for (std::size_t index = 0; index < pendingCount; ++index) {
            const auto& placement = pending[index];
            SetWindowPos(placement.control, nullptr, placement.x, placement.y,
                         placement.width, placement.height, placement.flags);
        }
    };
    HDWP deferred = BeginDeferWindowPos(static_cast<int>(pendingCount));
    if (!deferred) {
        applyIndividually();
        return;
    }
    for (std::size_t index = 0; index < pendingCount; ++index) {
        const auto& placement = pending[index];
        deferred = DeferWindowPos(deferred, placement.control, nullptr,
                                  placement.x, placement.y, placement.width, placement.height,
                                  placement.flags);
        if (!deferred) {
            applyIndividually();
            return;
        }
    }
    if (!EndDeferWindowPos(deferred)) applyIndividually();
}

class BufferedDrawSurface final {
public:
    BufferedDrawSurface(HDC target, RECT bounds) : target_(target), bounds_(bounds) {
        const int width = bounds_.right - bounds_.left;
        const int height = bounds_.bottom - bounds_.top;
        if (!target_ || width <= 0 || height <= 0) return;
        buffer_ = CreateCompatibleDC(target_);
        if (!buffer_) return;
        bitmap_ = CreateCompatibleBitmap(target_, width, height);
        if (!bitmap_) {
            DeleteDC(buffer_);
            buffer_ = nullptr;
            return;
        }
        previousBitmap_ = SelectObject(buffer_, bitmap_);
        if (!previousBitmap_ || previousBitmap_ == HGDI_ERROR) {
            DeleteObject(bitmap_);
            DeleteDC(buffer_);
            bitmap_ = nullptr;
            buffer_ = nullptr;
            previousBitmap_ = nullptr;
            return;
        }
        SetWindowOrgEx(buffer_, bounds_.left, bounds_.top, nullptr);
    }

    ~BufferedDrawSurface() {
        if (!buffer_) return;
        BitBlt(target_, bounds_.left, bounds_.top,
               bounds_.right - bounds_.left, bounds_.bottom - bounds_.top,
               buffer_, bounds_.left, bounds_.top, SRCCOPY);
        SelectObject(buffer_, previousBitmap_);
        DeleteObject(bitmap_);
        DeleteDC(buffer_);
    }

    BufferedDrawSurface(const BufferedDrawSurface&) = delete;
    BufferedDrawSurface& operator=(const BufferedDrawSurface&) = delete;

    [[nodiscard]] HDC Dc() const noexcept { return buffer_ ? buffer_ : target_; }

private:
    HDC target_{nullptr};
    RECT bounds_{};
    HDC buffer_{nullptr};
    HBITMAP bitmap_{nullptr};
    HGDIOBJ previousBitmap_{nullptr};
};

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"<invalid UTF-8>";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::wstring DisplayLabel(std::string_view value, std::wstring_view fallback) {
    auto result = Utf8ToWide(value);
    for (auto& character : result)
        if (character < L' ' || character == L'\x7f') character = L' ';
    if (result.empty()) result = fallback;
    if (result.size() > 240) {
        result.resize(237);
        result += L"...";
    }
    return result;
}

bool IsStyledListId(UINT id) {
    return (id >= static_cast<UINT>(kSourceSeasonList) && id <= static_cast<UINT>(kSourceStreamList)) ||
           id == static_cast<UINT>(kOverlayList);
}

void AddListText(HWND list, const std::wstring& text) {
    if (!list) return;
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    HDC dc = GetDC(list);
    if (!dc) return;
    const auto font = reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0));
    const HGDIOBJ previousFont = font ? SelectObject(dc, font) : nullptr;
    SIZE extent{};
    if (GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &extent)) {
        const int padding = MulDiv(28, static_cast<int>(std::max(1U, GetDpiForWindow(list))), 96);
        const auto current = static_cast<int>(SendMessageW(list, LB_GETHORIZONTALEXTENT, 0, 0));
        if (extent.cx + padding > current) SendMessageW(list, LB_SETHORIZONTALEXTENT, extent.cx + padding, 0);
    }
    if (previousFont) SelectObject(dc, previousFont);
    ReleaseDC(list, dc);
}

std::string WideToUtf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    (void)WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

std::wstring TimeText(double seconds) {
    const auto total = std::max<std::int64_t>(0, static_cast<std::int64_t>(seconds));
    const auto hours = total / 3600;
    const auto minutes = (total / 60) % 60;
    const auto remaining = total % 60;
    return hours > 0 ? std::format(L"{}:{:02}:{:02}", hours, minutes, remaining)
                     : std::format(L"{:02}:{:02}", minutes, remaining);
}

LRESULT CALLBACK VideoSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                               UINT_PTR, DWORD_PTR reference) {
    const HWND owner = reinterpret_cast<HWND>(reference);
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_KEYDOWN) {
        if (message != WM_MOUSEMOVE) SetFocus(owner);
        PostMessageW(owner, message, wParam, lParam);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK InteractionSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR, DWORD_PTR reference) {
    const HWND owner = reinterpret_cast<HWND>(reference);
    const bool timeline = GetDlgCtrlID(window) == kTimeline;
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_MOUSEMOVE) {
        if (!GetPropW(window, kHoverProperty)) {
            (void)SetPropW(window, kHoverProperty,
                           reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
            TrackMouseEvent(&tracking);
            InvalidateRect(window, nullptr, FALSE);
        }
    } else if (message == WM_MOUSELEAVE) {
        RemovePropW(window, kHoverProperty);
        InvalidateRect(window, nullptr, FALSE);
    } else if (message == WM_NCDESTROY) {
        RemovePropW(window, kHoverProperty);
        RemovePropW(window, kTimelineDragProperty);
    }
    if (timeline) {
        if (message == WM_MOUSEMOVE)
            PostMessageW(owner, kTimelineHoverMessage, TRUE, GET_X_LPARAM(lParam));
        else if (message == WM_MOUSELEAVE)
            PostMessageW(owner, kTimelineHoverMessage, FALSE, 0);

        if (message == WM_LBUTTONDOWN && IsWindowEnabled(window)) {
            SetFocus(window);
            SetCapture(window);
            (void)SetPropW(window, kTimelineDragProperty,
                           reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1)));
            SendMessageW(owner, kTimelineSeekMessage,
                         static_cast<WPARAM>(TimelineInput::Begin), GET_X_LPARAM(lParam));
            return 0;
        }
        if (message == WM_MOUSEMOVE && GetPropW(window, kTimelineDragProperty)) {
            SendMessageW(owner, kTimelineSeekMessage,
                         static_cast<WPARAM>(TimelineInput::Update), GET_X_LPARAM(lParam));
            return 0;
        }
        if (message == WM_LBUTTONUP && GetPropW(window, kTimelineDragProperty)) {
            RemovePropW(window, kTimelineDragProperty);
            if (GetCapture() == window) ReleaseCapture();
            SendMessageW(owner, kTimelineSeekMessage,
                         static_cast<WPARAM>(TimelineInput::Commit), GET_X_LPARAM(lParam));
            return 0;
        }
        if ((message == WM_CAPTURECHANGED || message == WM_CANCELMODE) &&
            GetPropW(window, kTimelineDragProperty)) {
            RemovePropW(window, kTimelineDragProperty);
            SendMessageW(owner, kTimelineSeekMessage,
                         static_cast<WPARAM>(TimelineInput::Cancel), 0);
        }
    }
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_POINTERDOWN ||
        message == WM_POINTERUPDATE || message == WM_SETFOCUS)
        PostMessageW(owner, kInteractionMessage,
                     message == WM_MOUSEMOVE ? TRUE : FALSE, 0);
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK SourceSelectorSubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR, DWORD_PTR reference) {
    const HWND owner = reinterpret_cast<HWND>(reference);
    if (message == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            PostMessageW(owner, WM_COMMAND, MAKEWPARAM(kSourceCancel, BN_CLICKED),
                         reinterpret_cast<LPARAM>(window));
            return 0;
        }
        if (wParam == VK_RETURN && GetDlgCtrlID(window) == kSourceStreamList) {
            PostMessageW(owner, WM_COMMAND, MAKEWPARAM(kSourceOpen, BN_CLICKED),
                         reinterpret_cast<LPARAM>(window));
            return 0;
        }
        if (wParam == VK_TAB) {
            const BOOL previous = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (const HWND next = GetNextDlgTabItem(owner, window, previous); next) SetFocus(next);
            return 0;
        }
    }
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_SETFOCUS)
        PostMessageW(owner, kInteractionMessage, 0, 0);
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK OverlaySubclass(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                 UINT_PTR, DWORD_PTR reference) {
    const HWND owner = reinterpret_cast<HWND>(reference);
    if (message == WM_KEYDOWN) {
        if (wParam == VK_ESCAPE) {
            PostMessageW(owner, WM_COMMAND, MAKEWPARAM(kOverlaySecondary, BN_CLICKED),
                         reinterpret_cast<LPARAM>(window));
            return 0;
        }
        if (wParam == VK_RETURN) {
            PostMessageW(owner, WM_COMMAND, MAKEWPARAM(kOverlayPrimary, BN_CLICKED),
                         reinterpret_cast<LPARAM>(window));
            return 0;
        }
        if (wParam == VK_TAB) {
            const BOOL previous = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (const HWND next = GetNextDlgTabItem(owner, window, previous); next) SetFocus(next);
            return 0;
        }
    }
    if (message == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE)) return 0;
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_SETFOCUS)
        PostMessageW(owner, kInteractionMessage, 0, 0);
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace

PlayerWindow::PlayerWindow(AppPaths paths, Config config, Logger& logger)
    : paths_(std::move(paths)), config_(std::move(config)), logger_(logger),
      shaders_(paths_.shaders, paths_.presets / "shaders.json"),
      ytDlp_(paths_.tools / "yt-dlp.exe"), resolvers_(&ytDlp_), engine_(paths_, config_, logger_) {
    shaders_.Reload();
    resolvers_.Add(std::make_unique<DirectMediaResolver>());
    resolvers_.Add(std::make_unique<AnimeGoResolver>());
    resolvers_.Add(std::make_unique<BrowserEmbedResolver>(paths_.cache / "webview2"));
    resolvers_.Add(std::make_unique<CdnVideoHubResolver>());
    resolvers_.Add(std::make_unique<AniBoomResolver>());
    resolvers_.Add(std::make_unique<YummyAnimeResolver>());
    resolvers_.Add(std::make_unique<GenericResolver>());
}

PlayerWindow::~PlayerWindow() {
    closing_ = true;
    if (resolverThread_.joinable()) { resolverThread_.request_stop(); resolverThread_.join(); }
    engine_.Shutdown();
    if (font_) DeleteObject(font_);
    if (iconFont_) DeleteObject(iconFont_);
    if (titleFont_) DeleteObject(titleFont_);
    if (backgroundBrush_) DeleteObject(backgroundBrush_);
    if (panelBrush_) DeleteObject(panelBrush_);
    if (statisticsBrush_) DeleteObject(statisticsBrush_);
    if (gdiplusToken_) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
}

void PlayerWindow::Create(HINSTANCE instance, int showCommand, bool backgroundTest) {
    instance_ = instance;
    Gdiplus::GdiplusStartupInput graphicsStartup;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &graphicsStartup, nullptr) != Gdiplus::Ok)
        throw std::runtime_error("Unable to initialize anti-aliased UI rendering");
    BOOL clientAnimations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &clientAnimations, 0))
        animationsEnabled_ = clientAnimations != FALSE;
    WNDCLASSEXW type{sizeof(type)};
    type.style = CS_DBLCLKS;
    type.lpfnWndProc = WindowProcedure;
    type.hInstance = instance;
    type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    type.lpszClassName = L"WannaViewer.PlayerWindow";
    type.hIconSm = type.hIcon;
    if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        throw std::runtime_error("Unable to register the player window class");
    const DWORD extendedStyle = backgroundTest ? WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE : 0;
    window_ = CreateWindowExW(extendedStyle, type.lpszClassName, L"WannaViewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, instance, this);
    if (!window_) throw std::runtime_error("Unable to create the player window");
    dpi_ = GetDpiForWindow(window_);
    const BOOL darkTitleBar = TRUE;
    (void)DwmSetWindowAttribute(window_, static_cast<DWMWINDOWATTRIBUTE>(20), &darkTitleBar, sizeof(darkTitleBar));
    (void)DwmSetWindowAttribute(window_, kDwmWindowCornerPreference,
                                &kDwmRoundCorners, sizeof(kDwmRoundCorners));
    CreateControls();
    DragAcceptFiles(window_, TRUE);
    LogHardwareInformation();
    if (backgroundTest) {
        const int offscreenX = GetSystemMetrics(SM_XVIRTUALSCREEN) +
                               GetSystemMetrics(SM_CXVIRTUALSCREEN) + 1024;
        const int offscreenY = GetSystemMetrics(SM_YVIRTUALSCREEN) +
                               GetSystemMetrics(SM_CYVIRTUALSCREEN) + 1024;
        SetWindowPos(window_, nullptr, offscreenX, offscreenY, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
    ShowWindow(window_, backgroundTest ? SW_SHOWNOACTIVATE : showCommand);
    UpdateWindow(window_);
    RecordInteraction();
}

void PlayerWindow::OpenInitial(std::string value) { initial_ = std::move(value); }

void PlayerWindow::EnableBenchmark(std::string value, std::string mode) {
    benchmarkMode_ = true;
    benchmarkInput_ = value;
    benchmarkProfile_ = std::move(mode);
    initial_ = std::move(value);
    SetWindowTextW(window_, L"WannaViewer — benchmark");
    UpdateActiveTimer();
}

int PlayerWindow::Run() {
    if (initial_) {
        auto value = std::move(*initial_);
        initial_.reset();
        ResolveUrl(std::move(value));
    }
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK PlayerWindow::WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<PlayerWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<PlayerWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->HandleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

void PlayerWindow::CreateControls() {
    backgroundBrush_ = CreateSolidBrush(kWindowColor);
    panelBrush_ = CreateSolidBrush(kPanelColor);
    statisticsBrush_ = CreateSolidBrush(kStatisticsColor);
    video_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_LEFT,
                             0, 0, 100, 100, window_, nullptr, instance_, nullptr);
    SetWindowSubclass(video_, VideoSubclass, 1, reinterpret_cast<DWORD_PTR>(window_));
    controlsBar_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW,
                                   0, 0, 100, 96, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kControlsBar)), instance_, nullptr);
    const auto createButton = [this](int id, const wchar_t* text) {
        return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | BS_OWNERDRAW | BS_FLAT,
                               0, 0, 80, 34, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    playButton_ = createButton(kPlay, L"Play");
    rewindButton_ = createButton(kRewind, L"Back 10 seconds");
    forwardButton_ = createButton(kForward, L"Forward 10 seconds");
    muteButton_ = createButton(kMute, L"Volume");
    timeline_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                0, 0, 200, 24, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTimeline)), instance_, nullptr);
    SendMessageW(timeline_, TBM_SETRANGE, TRUE, MAKELONG(0, 10000));
    timeLabel_ = CreateWindowExW(0, L"STATIC", L"00:00  /  00:00", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_CENTER | SS_CENTERIMAGE,
                                 0, 0, 112, 28, window_, nullptr, instance_, nullptr);
    volume_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                              0, 0, 80, 24, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVolume)), instance_, nullptr);
    SendMessageW(volume_, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(volume_, TBM_SETPOS, TRUE, 80);
    audio_ = createButton(kAudio, L"Audio");
    subtitles_ = createButton(kSubtitles, L"Subtitles");
    videoQuality_ = createButton(kVideoQuality, L"Video");
    shader_ = createButton(kShader, L"Shaders");
    statsButton_ = createButton(kStatistics, L"Stats");
    fullscreenButton_ = createButton(kFullscreen, L"Full screen");
    settingsButton_ = createButton(kSettings, L"Settings");
    stats_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS | SS_LEFT,
                             16, 16, 520, 270, window_, nullptr, instance_, nullptr);
    emptyState_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SS_OWNERDRAW,
                                  0, 0, 540, 236, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEmptyState)), instance_, nullptr);
    openFileButton_ = createButton(kOpenFile, L"Open file");
    openUrlButton_ = createButton(kOpenUrl, L"Open URL");
    sourcePanel_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW,
                                   0, 0, 100, 100, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSourcePanel)), instance_, nullptr);
    const auto createSourceLabel = [this](int id, const wchar_t* text, DWORD style) {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_CLIPSIBLINGS | style,
                               0, 0, 100, 24, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    sourceTitle_ = createSourceLabel(kSourceTitle, L"Choose a playback source", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    sourceSubtitle_ = createSourceLabel(kSourceSubtitle, L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    sourceSeasonLabel_ = createSourceLabel(kSourceSeasonLabel, L"Season", SS_LEFT | SS_CENTERIMAGE);
    sourceVoiceLabel_ = createSourceLabel(kSourceVoiceLabel, L"Voice", SS_LEFT | SS_CENTERIMAGE);
    sourceEpisodeLabel_ = createSourceLabel(kSourceEpisodeLabel, L"Episode", SS_LEFT | SS_CENTERIMAGE);
    sourceStreamLabel_ = createSourceLabel(kSourceStreamLabel, L"Source", SS_LEFT | SS_CENTERIMAGE);
    sourceStatus_ = createSourceLabel(kSourceStatus, L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    const auto createSourceList = [this](int id) {
        constexpr DWORD style = WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
        return CreateWindowExW(0, L"LISTBOX", nullptr, style, 0, 0, 100, 100, window_,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    };
    sourceSeasonList_ = createSourceList(kSourceSeasonList);
    sourceVoiceList_ = createSourceList(kSourceVoiceList);
    sourceEpisodeList_ = createSourceList(kSourceEpisodeList);
    sourceStreamList_ = createSourceList(kSourceStreamList);
    for (HWND control : {sourceSeasonList_, sourceVoiceList_, sourceEpisodeList_, sourceStreamList_})
        (void)SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    sourceOpenButton_ = createButton(kSourceOpen, L"Open stream");
    sourceCancelButton_ = createButton(kSourceCancel, L"Cancel");
    overlayPanel_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_CLIPSIBLINGS | SS_OWNERDRAW,
                                    0, 0, 100, 100, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverlayPanel)), instance_, nullptr);
    overlayTitle_ = createSourceLabel(kOverlayTitle, L"", SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    overlayBody_ = createSourceLabel(kOverlayBody, L"", SS_LEFT | SS_EDITCONTROL);
    overlayEdit_ = CreateWindowExW(0, L"EDIT", L"https://",
                                   WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | ES_AUTOHSCROLL,
                                   0, 0, 100, 42, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverlayEdit)), instance_, nullptr);
    SendMessageW(overlayEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(Scale(10), Scale(10)));
    constexpr DWORD overlayListStyle = WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                       LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
    overlayList_ = CreateWindowExW(0, L"LISTBOX", nullptr, overlayListStyle,
                                   0, 0, 100, 100, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOverlayList)), instance_, nullptr);
    (void)SetWindowTheme(overlayList_, L"DarkMode_Explorer", nullptr);
    overlayPrimaryButton_ = createButton(kOverlayPrimary, L"Apply");
    overlaySecondaryButton_ = createButton(kOverlaySecondary, L"Cancel");
    for (HWND control : {playButton_, rewindButton_, forwardButton_, muteButton_, timeline_, volume_, audio_, subtitles_, videoQuality_, shader_,
                         statsButton_, fullscreenButton_, settingsButton_, openFileButton_, openUrlButton_,
                         sourceOpenButton_, sourceCancelButton_, overlayPrimaryButton_, overlaySecondaryButton_}) {
        (void)SetWindowTheme(control, L"", L"");
        SetWindowSubclass(control, InteractionSubclass, 2, reinterpret_cast<DWORD_PTR>(window_));
    }
    for (HWND control : {sourceSeasonList_, sourceVoiceList_, sourceEpisodeList_, sourceStreamList_})
        SetWindowSubclass(control, SourceSelectorSubclass, 3, reinterpret_cast<DWORD_PTR>(window_));
    for (HWND control : {sourceOpenButton_, sourceCancelButton_})
        SetWindowSubclass(control, SourceSelectorSubclass, 3, reinterpret_cast<DWORD_PTR>(window_));
    for (HWND control : {overlayEdit_, overlayList_, overlayPrimaryButton_, overlaySecondaryButton_})
        SetWindowSubclass(control, OverlaySubclass, 4, reinterpret_cast<DWORD_PTR>(window_));
    tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               window_, nullptr, instance_, nullptr);
    if (tooltip_) {
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, Scale(360));
        SetWindowPos(tooltip_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    AddTooltip(playButton_, L"Play or pause (Space)");
    AddTooltip(rewindButton_, L"Back 10 seconds (Shift+Left for 30)");
    AddTooltip(forwardButton_, L"Forward 10 seconds (Shift+Right for 30)");
    AddTooltip(muteButton_, L"Mute or restore sound (M)");
    AddTooltip(timeline_, L"Seek through the current video");
    AddTooltip(volume_, L"Volume");
    AddTooltip(audio_, L"Choose audio track (A cycles)");
    AddTooltip(subtitles_, L"Choose subtitles (S cycles)");
    AddTooltip(videoQuality_, L"Choose video track or quality");
    AddTooltip(shader_, L"Anime4K and custom shaders (Ctrl+0…4)");
    AddTooltip(statsButton_, L"Playback statistics (F10)");
    AddTooltip(fullscreenButton_, L"Full screen (F)");
    AddTooltip(settingsButton_, L"Playback and cache settings");
    CreateFonts();
    EnableWindow(playButton_, FALSE);
    EnableWindow(rewindButton_, FALSE);
    EnableWindow(forwardButton_, FALSE);
    EnableWindow(muteButton_, FALSE);
    EnableWindow(timeline_, FALSE);
    EnableWindow(volume_, FALSE);
    EnableWindow(audio_, FALSE);
    EnableWindow(subtitles_, FALSE);
    EnableWindow(videoQuality_, FALSE);
    EnableWindow(shader_, FALSE);
    EnableWindow(statsButton_, FALSE);
    EnableWindow(sourceOpenButton_, FALSE);
    SetWindowPos(video_, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    LayoutControls();
}

void PlayerWindow::EnsureEngineInitialized() {
    if (engine_.IsInitialized()) return;
    engine_.Initialize(reinterpret_cast<std::uintptr_t>(video_), [this](PlaybackEvent event) {
        if (closing_.load()) return;
        auto payload = std::make_unique<PlaybackEvent>(std::move(event));
        if (PostMessageW(window_, kPlaybackMessage, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
    });
}

void PlayerWindow::OpenMedia(std::string value,
                             std::vector<std::pair<std::string, std::string>> headers,
                             std::string externalAudioUrl) {
    if (sourceSelection_) HideSourceSelector();
    if (overlayMode_ != OverlayMode::None) HideOverlay();
    mediaOpening_ = true;
    playbackStarted_ = false;
    startupTimeoutReported_ = false;
    playbackLoadStarted_ = std::chrono::steady_clock::now();
    LayoutControls();
    UpdateActiveTimer();
    try {
        EnsureEngineInitialized();
        engine_.Open(value, headers, externalAudioUrl);
    } catch (...) {
        mediaOpening_ = false;
        LayoutControls();
        UpdateActiveTimer();
        throw;
    }
}

void PlayerWindow::CreateFonts() {
    if (font_) DeleteObject(font_);
    if (iconFont_) DeleteObject(iconFont_);
    if (titleFont_) DeleteObject(titleFont_);
    font_ = CreateFontW(-Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    iconFont_ = CreateFontW(-Scale(11), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    titleFont_ = CreateFontW(-Scale(26), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    for (HWND control : {playButton_, rewindButton_, forwardButton_, muteButton_, timeLabel_, audio_, subtitles_, videoQuality_, shader_, statsButton_,
                         fullscreenButton_, settingsButton_, stats_, openFileButton_, openUrlButton_, sourceTitle_,
                         sourceSubtitle_, sourceSeasonLabel_, sourceVoiceLabel_, sourceEpisodeLabel_, sourceStreamLabel_,
                         sourceSeasonList_, sourceVoiceList_, sourceEpisodeList_, sourceStreamList_, sourceStatus_,
                         sourceOpenButton_, sourceCancelButton_, overlayTitle_, overlayBody_, overlayEdit_, overlayList_,
                         overlayPrimaryButton_, overlaySecondaryButton_})
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    if (sourceTitle_) SendMessageW(sourceTitle_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    if (overlayTitle_) SendMessageW(overlayTitle_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
}

void PlayerWindow::AddTooltip(HWND control, const wchar_t* text) {
    if (!tooltip_ || !control) return;
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    tool.hwnd = window_;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tooltip_, TTM_ADDTOOL, 0, reinterpret_cast<LPARAM>(&tool));
}

int PlayerWindow::Scale(int value) const noexcept {
    return MulDiv(value, static_cast<int>(std::max(1U, dpi_)), 96);
}

void PlayerWindow::LayoutControls() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const bool selectorVisible = sourceSelection_.has_value();
    const bool overlayVisible = overlayMode_ != OverlayMode::None;
    const bool chromeVisible = mediaLoaded_ && !selectorVisible && !overlayVisible &&
                               (controlsVisible_ || controlsAnimationProgress_ > 0.0);
    const auto layout = ui::ComputeControlLayout(width, height, dpi_, chromeVisible);
    compactLayout_ = layout.compact;
    std::array<WindowPlacement, kMaxWindowPlacements> placements{};
    std::size_t placementCount = 0;
    const auto place = [&placements, &placementCount](HWND control, ui::Rect rectangle) {
        if (placementCount < placements.size())
            placements[placementCount++] = {control, rectangle};
    };
    const auto inset = [](ui::Rect rectangle, int amount) {
        if (rectangle.width <= amount * 2 || rectangle.height <= amount * 2) return rectangle;
        rectangle.x += amount;
        rectangle.y += amount;
        rectangle.width -= amount * 2;
        rectangle.height -= amount * 2;
        return rectangle;
    };
    place(video_, layout.video);
    const int chromeOffset = chromeVisible
        ? static_cast<int>(std::lround(static_cast<double>(layout.bar.height) *
                                      (1.0 - std::clamp(controlsAnimationProgress_, 0.0, 1.0))))
        : 0;
    const auto animated = [chromeOffset](ui::Rect rectangle) {
        if (rectangle.visible) rectangle.y += chromeOffset;
        return rectangle;
    };
    place(controlsBar_, animated(layout.bar));
    place(timeline_, animated(layout.timeline));
    place(timeLabel_, animated(layout.time));
    place(playButton_, animated(layout.play));
    place(rewindButton_, animated(layout.rewind));
    place(forwardButton_, animated(layout.forward));
    place(muteButton_, animated(layout.mute));
    place(volume_, animated(layout.volume));
    place(audio_, animated(layout.audio));
    place(subtitles_, animated(layout.subtitles));
    place(videoQuality_, animated(layout.quality));
    place(shader_, animated(layout.shaders));
    place(statsButton_, animated(layout.statistics));
    place(fullscreenButton_, animated(layout.fullscreen));
    place(settingsButton_, animated(layout.settings));

    const bool showEmpty = !mediaLoaded_ && !mediaOpening_ && !benchmarkMode_ && !selectorVisible && !overlayVisible;
    auto emptyState = layout.emptyState;
    auto openFile = layout.openFile;
    auto openUrl = layout.openUrl;
    emptyState.visible = openFile.visible = openUrl.visible = showEmpty;
    place(emptyState_, emptyState);
    place(openFileButton_, openFile);
    place(openUrlButton_, openUrl);

    const auto selector = ui::ComputeSourceSelectorLayout(width, layout.bar.visible ? layout.bar.y : height,
                                                           dpi_, selectorVisible);
    if (selectorVisible) {
        const auto rowHeight = static_cast<WPARAM>(Scale(selector.compact ? 30 : 34));
        for (HWND list : {sourceSeasonList_, sourceVoiceList_, sourceEpisodeList_, sourceStreamList_})
            if (SendMessageW(list, LB_GETITEMHEIGHT, 0, 0) != static_cast<LRESULT>(rowHeight))
                SendMessageW(list, LB_SETITEMHEIGHT, 0, rowHeight);
    }
    place(sourcePanel_, selector.panel);
    place(sourceTitle_, selector.title);
    place(sourceSubtitle_, selector.subtitle);
    place(sourceSeasonLabel_, selector.seasonLabel);
    place(sourceVoiceLabel_, selector.voiceLabel);
    place(sourceEpisodeLabel_, selector.episodeLabel);
    place(sourceStreamLabel_, selector.sourceLabel);
    place(sourceSeasonList_, inset(selector.seasonList, Scale(4)));
    place(sourceVoiceList_, inset(selector.voiceList, Scale(4)));
    place(sourceEpisodeList_, inset(selector.episodeList, Scale(4)));
    place(sourceStreamList_, inset(selector.sourceList, Scale(4)));
    place(sourceStatus_, selector.status);
    place(sourceOpenButton_, selector.open);
    place(sourceCancelButton_, selector.cancel);

    const ui::OverlayContent overlayContent = overlayMode_ == OverlayMode::Choice ? ui::OverlayContent::Choice
        : overlayMode_ == OverlayMode::Url ? ui::OverlayContent::Input : ui::OverlayContent::Message;
    const auto overlay = ui::ComputeOverlayLayout(width, height, dpi_, overlayVisible, overlayContent);
    if (overlayMode_ == OverlayMode::Choice)
        if (const auto rowHeight = static_cast<LPARAM>(Scale(38));
            SendMessageW(overlayList_, LB_GETITEMHEIGHT, 0, 0) != rowHeight)
            SendMessageW(overlayList_, LB_SETITEMHEIGHT, 0, rowHeight);
    place(overlayPanel_, overlay.panel);
    place(overlayTitle_, overlay.title);
    place(overlayBody_, overlay.body);
    place(overlayEdit_, inset(overlay.edit, Scale(4)));
    place(overlayList_, inset(overlay.list, Scale(4)));
    place(overlayPrimaryButton_, overlay.primary);
    place(overlaySecondaryButton_, overlay.secondary);

    place(stats_, {Scale(16), Scale(16), std::max(0, std::min(Scale(560), width - Scale(32))),
                   std::max(0, std::min(Scale(290), layout.video.height - Scale(32))),
                   statisticsVisible_ && !selectorVisible && !overlayVisible});
    ApplyWindowLayout(window_, placements, placementCount);
}

void PlayerWindow::SetMediaLoaded(bool loaded) {
    mediaLoaded_ = loaded;
    mediaOpening_ = false;
    displayedTimelinePosition_ = -1;
    displayedPlaying_.reset();
    displayedTimeLabel_.clear();
    EnableWindow(playButton_, loaded);
    EnableWindow(rewindButton_, loaded);
    EnableWindow(forwardButton_, loaded);
    EnableWindow(muteButton_, loaded);
    EnableWindow(timeline_, loaded);
    EnableWindow(volume_, loaded);
    EnableWindow(audio_, loaded && !audioTrackIds_.empty());
    EnableWindow(subtitles_, loaded);
    EnableWindow(videoQuality_, loaded && !videoTrackIds_.empty());
    EnableWindow(shader_, loaded);
    EnableWindow(statsButton_, loaded);
    if (!loaded) {
        controlsVisible_ = true;
        controlsAnimationFrom_ = 1.0;
        controlsAnimationProgress_ = 1.0;
        controlsAnimationDuration_ = 0;
        timelineDragging_ = false;
        timelineHovering_ = false;
        timelineAnimationFrom_ = 0.0;
        timelineAnimationProgress_ = 0.0;
        timelineAnimationTarget_ = 0.0;
        timelineAnimationDuration_ = 0;
        timelineSeekPreviewUntil_ = 0;
    }
    LayoutControls();
    UpdateActiveTimer();
}

void PlayerWindow::ShowControls(bool show) {
    if (!show && (sourceSelection_ || overlayMode_ != OverlayMode::None)) return;
    if (controlsVisible_ == show) return;
    const ULONGLONG now = GetTickCount64();
    UpdateControlsAnimation(now);
    if (!show) {
        POINT cursor{};
        if (GetCursorPos(&cursor)) {
            lastMousePosition_ = cursor;
            hasLastMousePosition_ = true;
        }
    }
    controlsVisible_ = show;
    const double target = show ? 1.0 : 0.0;
    const ULONGLONG fullDuration = show ? kControlsShowAnimationMs : kControlsHideAnimationMs;
    const double remaining = std::abs(target - controlsAnimationProgress_);
    controlsAnimationFrom_ = controlsAnimationProgress_;
    controlsAnimationStarted_ = now;
    controlsAnimationDuration_ = animationsEnabled_
        ? static_cast<ULONGLONG>(std::lround(static_cast<double>(fullDuration) * remaining))
        : 0;
    if (controlsAnimationDuration_ == 0) controlsAnimationProgress_ = target;
    LayoutControls();
    if (!show) SetFocus(window_);
    UpdateActiveTimer();
}

bool PlayerWindow::ControlsAnimationActive() const noexcept {
    return controlsAnimationDuration_ != 0;
}

void PlayerWindow::UpdateControlsAnimation(ULONGLONG now) {
    if (!ControlsAnimationActive()) return;
    const double elapsed = static_cast<double>(now - controlsAnimationStarted_);
    const double linear = std::clamp(elapsed / static_cast<double>(controlsAnimationDuration_), 0.0, 1.0);
    const double eased = linear * linear * (3.0 - 2.0 * linear);
    const double target = controlsVisible_ ? 1.0 : 0.0;
    controlsAnimationProgress_ = controlsAnimationFrom_ + (target - controlsAnimationFrom_) * eased;
    if (linear >= 1.0) {
        controlsAnimationProgress_ = target;
        controlsAnimationDuration_ = 0;
    }
    LayoutControls();
}

void PlayerWindow::UpdateTimelineFromPoint(int clientX, bool commit) {
    if (!timeline_) return;
    timelineHoverX_ = clientX;
    RECT client{};
    GetClientRect(timeline_, &client);
    const int minimum = static_cast<int>(SendMessageW(timeline_, TBM_GETRANGEMIN, 0, 0));
    const int maximum = static_cast<int>(SendMessageW(timeline_, TBM_GETRANGEMAX, 0, 0));
    const int channelLeft = client.left + Scale(kTimelineChannelInset);
    const int channelRight = client.right - Scale(kTimelineChannelInset);
    UpdateTimelineFromValue(
        ui::TimelineValueFromPoint(clientX, channelLeft, channelRight, minimum, maximum), commit);
}

void PlayerWindow::UpdateTimelineFromValue(int value, bool commit) {
    if (!timeline_ || !mediaLoaded_) return;
    const int minimum = static_cast<int>(SendMessageW(timeline_, TBM_GETRANGEMIN, 0, 0));
    const int maximum = static_cast<int>(SendMessageW(timeline_, TBM_GETRANGEMAX, 0, 0));
    value = std::clamp(value, minimum, maximum);
    const double duration = engine_.Duration();
    if (duration <= 0.0 || maximum <= minimum) {
        timelineDragging_ = false;
        RecordInteraction();
        return;
    }

    if (displayedTimelinePosition_ != value) {
        displayedTimelinePosition_ = value;
        SendMessageW(timeline_, TBM_SETPOS, TRUE, value);
    }
    const double ratio = static_cast<double>(value - minimum) / static_cast<double>(maximum - minimum);
    timelinePreviewSeconds_ = duration * ratio;
    const auto label = TimeText(timelinePreviewSeconds_) + L"  /  " + TimeText(duration);
    if (displayedTimeLabel_ != label) {
        displayedTimeLabel_ = label;
        SetWindowTextW(timeLabel_, displayedTimeLabel_.c_str());
    }

    timelineDragging_ = !commit;
    const ULONGLONG now = GetTickCount64();
    if (commit) {
        timelineSeekPreviewUntil_ = now + kTimelineSeekPreviewMs;
        engine_.SeekAbsolute(timelinePreviewSeconds_);
    }
    SetTimelineAnimationTarget(timelineDragging_ || timelineHovering_, now);
    RecordInteraction();
}

void PlayerWindow::CancelTimelineDrag() {
    if (!timelineDragging_) return;
    timelineDragging_ = false;
    timelineSeekPreviewUntil_ = 0;
    SetTimelineAnimationTarget(timelineHovering_, GetTickCount64());
    UpdateUi();
    RecordInteraction();
}

void PlayerWindow::SetTimelineAnimationTarget(bool active, ULONGLONG now) {
    const double target = active ? 1.0 : 0.0;
    if (timelineAnimationTarget_ == target) return;
    UpdateTimelineAnimation(now);
    timelineAnimationFrom_ = timelineAnimationProgress_;
    timelineAnimationTarget_ = target;
    timelineAnimationStarted_ = now;
    const double remaining = std::abs(target - timelineAnimationProgress_);
    timelineAnimationDuration_ = animationsEnabled_
        ? static_cast<ULONGLONG>(std::lround(static_cast<double>(kTimelineAnimationMs) * remaining))
        : 0;
    if (timelineAnimationDuration_ == 0) timelineAnimationProgress_ = target;
    InvalidateRect(timeline_, nullptr, FALSE);
    UpdateActiveTimer();
}

bool PlayerWindow::TimelineAnimationActive() const noexcept {
    return timelineAnimationDuration_ != 0;
}

void PlayerWindow::UpdateTimelineAnimation(ULONGLONG now) {
    if (!TimelineAnimationActive()) return;
    const double elapsed = static_cast<double>(now - timelineAnimationStarted_);
    const double linear = std::clamp(elapsed / static_cast<double>(timelineAnimationDuration_), 0.0, 1.0);
    const double eased = 1.0 - std::pow(1.0 - linear, 3.0);
    timelineAnimationProgress_ = timelineAnimationFrom_ +
                                 (timelineAnimationTarget_ - timelineAnimationFrom_) * eased;
    if (linear >= 1.0) {
        timelineAnimationProgress_ = timelineAnimationTarget_;
        timelineAnimationDuration_ = 0;
    }
    InvalidateRect(timeline_, nullptr, FALSE);
}

void PlayerWindow::RecordInteraction() {
    lastInteraction_ = GetTickCount64();
    ShowControls(true);
    UpdateActiveTimer();
}

void PlayerWindow::RecordMouseMovement() {
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        RecordInteraction();
        return;
    }
    if (hasLastMousePosition_ && cursor.x == lastMousePosition_.x && cursor.y == lastMousePosition_.y)
        return;
    lastMousePosition_ = cursor;
    hasLastMousePosition_ = true;
    RecordInteraction();
}

bool PlayerWindow::IsCursorOverControls() const noexcept {
    if (!controlsBar_ || !IsWindowVisible(controlsBar_)) return false;
    POINT cursor{};
    RECT controls{};
    return GetCursorPos(&cursor) && GetWindowRect(controlsBar_, &controls) &&
           PtInRect(&controls, cursor) != FALSE;
}

void PlayerWindow::UpdateActiveTimer() {
    const bool waitingForFirstFrame = (mediaOpening_ || mediaLoaded_) && !playbackStarted_;
    const UINT desiredInterval = ControlsAnimationActive() || TimelineAnimationActive()
        ? kControlsAnimationTimerMs
        : ((mediaLoaded_ && controlsVisible_) || waitingForFirstFrame || statisticsVisible_ || benchmarkMode_ ||
           sourceSelection_ || overlayMode_ != OverlayMode::None)
            ? kUiRefreshTimerMs
            : 0;
    if (desiredInterval == uiTimerInterval_) return;
    if (desiredInterval == 0)
        KillTimer(window_, kUiTimer);
    else
        SetTimer(window_, kUiTimer, desiredInterval, nullptr);
    uiTimerInterval_ = desiredInterval;
}

void PlayerWindow::UpdateUi() {
    const double duration = engine_.Duration();
    const double position = engine_.Position();
    const ULONGLONG now = GetTickCount64();
    const auto steadyNow = std::chrono::steady_clock::now();
    if ((mediaOpening_ || mediaLoaded_) && !playbackStarted_ && !startupTimeoutReported_ &&
        steadyNow - playbackLoadStarted_ > std::chrono::seconds(30)) {
        startupTimeoutReported_ = true;
        engine_.Stop();
        SetMediaLoaded(false);
        if (RetryBrowserProvider()) return;
        SetWindowTextW(window_, L"WannaViewer");
        ShowError(L"Playback", "Playback did not produce a frame within 30 seconds. The stream may have expired or rejected its media segments.");
        return;
    }
    if (controlsVisible_ && !timelineDragging_) {
        double displayPosition = position;
        if (timelineSeekPreviewUntil_ != 0) {
            const bool seekArrived = std::abs(position - timelinePreviewSeconds_) <= 1.0;
            if (now >= timelineSeekPreviewUntil_ || seekArrived || duration <= 0.0)
                timelineSeekPreviewUntil_ = 0;
            else
                displayPosition = timelinePreviewSeconds_;
        }
        const int trackPosition = duration > 0.0
            ? static_cast<int>(std::lround(std::clamp(displayPosition / duration, 0.0, 1.0) * 10000.0))
            : 0;
        if (displayedTimelinePosition_ != trackPosition) {
            displayedTimelinePosition_ = trackPosition;
            SendMessageW(timeline_, TBM_SETPOS, TRUE, trackPosition);
        }
        const auto label = TimeText(displayPosition) + L"  /  " + TimeText(duration);
        if (displayedTimeLabel_ != label) {
            displayedTimeLabel_ = label;
            SetWindowTextW(timeLabel_, displayedTimeLabel_.c_str());
        }
        const bool playing = mediaLoaded_ && !engine_.IsPaused();
        if (!displayedPlaying_ || *displayedPlaying_ != playing) {
            displayedPlaying_ = playing;
            SetWindowTextW(playButton_, playing ? L"Pause" : L"Play");
            InvalidateRect(playButton_, nullptr, FALSE);
        }
    }
    if (statisticsVisible_ && (lastStatisticsUpdate_ == std::chrono::steady_clock::time_point{} ||
        steadyNow - lastStatisticsUpdate_ >= std::chrono::milliseconds(500))) {
        lastStatisticsUpdate_ = steadyNow;
        const auto text = Utf8ToWide(engine_.Statistics().ToDisplayText());
        SetWindowTextW(stats_, text.c_str());
    }
    if (benchmarkRunning_ && steadyNow - lastBenchmarkSample_ >= std::chrono::seconds(1)) {
        lastBenchmarkSample_ = steadyNow;
        auto sample = engine_.Statistics();
        if (benchmarkDroppedBaseline_ < 0) {
            benchmarkDroppedBaseline_ = sample.droppedFrames;
            benchmarkDelayedBaseline_ = sample.delayedFrames;
        }
        benchmarkSamples_.push_back(std::move(sample));
    }
    if (benchmarkRunning_ && duration > 0.0 && position >= duration - 0.05) FinishBenchmark();
}

void PlayerWindow::UpdateTracks() {
    audioTrackIds_.clear();
    subtitleTrackIds_.clear();
    videoTrackIds_.clear();
    audioTrackLabels_.clear();
    subtitleTrackLabels_.clear();
    videoTrackLabels_.clear();
    subtitleTrackLabels_.push_back(L"Off");
    subtitleTrackIds_.push_back(-1);
    audioSelection_ = -1;
    subtitleSelection_ = 0;
    videoSelection_ = -1;
    for (const auto& track : engine_.Tracks()) {
        auto label = track.title.empty() ? track.language : track.title;
        if (label.empty() && track.type == "video" && track.height > 0)
            label = std::format("{}p {}", track.height, track.codec);
        if (label.empty()) label = std::format("{} {}", track.type, track.id);
        if (track.type == "audio") {
            audioTrackLabels_.push_back(Utf8ToWide(label));
            audioTrackIds_.push_back(track.id);
            if (track.selected) audioSelection_ = static_cast<int>(audioTrackIds_.size() - 1);
        } else if (track.type == "sub") {
            subtitleTrackLabels_.push_back(Utf8ToWide(label));
            subtitleTrackIds_.push_back(track.id);
            if (track.selected) subtitleSelection_ = static_cast<int>(subtitleTrackIds_.size() - 1);
        } else if (track.type == "video") {
            videoTrackLabels_.push_back(Utf8ToWide(label));
            videoTrackIds_.push_back(track.id);
            if (track.selected) videoSelection_ = static_cast<int>(videoTrackIds_.size() - 1);
        }
    }
    if (audioSelection_ < 0 && !audioTrackIds_.empty()) audioSelection_ = 0;
    if (videoSelection_ < 0 && !videoTrackIds_.empty()) videoSelection_ = 0;
    EnableWindow(audio_, mediaLoaded_ && !audioTrackIds_.empty());
    EnableWindow(subtitles_, mediaLoaded_);
    EnableWindow(videoQuality_, mediaLoaded_ && !videoTrackIds_.empty());
    InvalidateRect(audio_, nullptr, FALSE);
    InvalidateRect(subtitles_, nullptr, FALSE);
    InvalidateRect(videoQuality_, nullptr, FALSE);
}

void PlayerWindow::ShowAudioMenu() {
    ShowChoiceOverlay(L"Audio track", L"Choose the audio stream used for playback",
                      audioTrackLabels_, audioSelection_, OverlayAction::Audio);
}

void PlayerWindow::ShowSubtitleMenu() {
    ShowChoiceOverlay(L"Subtitles", L"Choose a subtitle track or turn subtitles off",
                      subtitleTrackLabels_, subtitleSelection_, OverlayAction::Subtitles);
}

void PlayerWindow::ShowVideoMenu() {
    ShowChoiceOverlay(L"Video track", L"Choose the video stream or quality",
                      videoTrackLabels_, videoSelection_, OverlayAction::Video);
}

void PlayerWindow::ShowShaderMenu() {
    std::vector<std::wstring> labels;
    labels.reserve(shaders_.Presets().size() + 1U);
    for (const auto& preset : shaders_.Presets()) labels.push_back(Utf8ToWide(preset.name));
    labels.push_back(L"Custom GLSL…");
    ShowChoiceOverlay(L"Video shaders", L"Choose a shader preset or load custom GLSL files",
                      std::move(labels), static_cast<int>(shaderPresetIndex_), OverlayAction::Shaders);
}

void PlayerWindow::ToggleFullscreen() {
    if (!fullscreen_) {
        previousStyle_ = static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE));
        GetWindowPlacement(window_, &previousPlacement_);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(window_, GWL_STYLE, previousStyle_ & ~static_cast<DWORD>(WS_OVERLAPPEDWINDOW));
        SetWindowPos(window_, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        fullscreen_ = true;
        ShowControls(false);
    } else {
        SetWindowLongPtrW(window_, GWL_STYLE, previousStyle_);
        SetWindowPlacement(window_, &previousPlacement_);
        SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        fullscreen_ = false;
        RecordInteraction();
    }
}

void PlayerWindow::ToggleStatistics() {
    if (!mediaLoaded_) return;
    statisticsVisible_ = !statisticsVisible_;
    if (statisticsVisible_) lastStatisticsUpdate_ = {};
    LayoutControls();
    UpdateActiveTimer();
}

void PlayerWindow::ApplyShaderHotkey(int hotkey) {
    if (!mediaLoaded_) return;
    const auto* preset = shaders_.ForHotkey(hotkey);
    if (!preset) return;
    const auto index = static_cast<std::size_t>(preset - shaders_.Presets().data());
    ApplyShaderPreset(index);
}

void PlayerWindow::ApplyShaderPreset(std::size_t index) {
    if (index == shaders_.Presets().size()) { OpenCustomShaders(); return; }
    if (index >= shaders_.Presets().size()) return;
    try {
        engine_.SetShaders(shaders_.Resolve(shaders_.Presets()[index]));
        shaderPresetIndex_ = index;
        InvalidateRect(shader_, nullptr, FALSE);
    } catch (const std::exception& error) {
        ShowError(L"Shader preset", error.what());
        engine_.SetShaders({});
        shaderPresetIndex_ = 0;
    }
}

void PlayerWindow::OpenCustomShaders() {
    std::wstring buffer(65536, L'\0');
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrFilter = L"mpv GLSL shaders\0*.glsl\0All files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_ALLOWMULTISELECT;
    if (!GetOpenFileNameW(&dialog)) return;
    const std::filesystem::path first(buffer.c_str());
    const wchar_t* cursor = buffer.c_str() + first.native().size() + 1;
    std::vector<std::filesystem::path> selected;
    if (*cursor == L'\0') selected.push_back(first);
    else {
        while (*cursor != L'\0') {
            std::filesystem::path name(cursor);
            selected.push_back(first / name);
            cursor += name.native().size() + 1;
        }
    }
    try {
        std::vector<std::filesystem::path> imported;
        std::filesystem::create_directories(paths_.shaders / "Custom");
        for (const auto& source : selected) {
            if (source.extension() != L".glsl" && source.extension() != L".GLSL")
                throw std::runtime_error("Only .glsl files can be imported");
            auto destination = paths_.shaders / "Custom" / source.filename();
            if (std::filesystem::weakly_canonical(source) != std::filesystem::weakly_canonical(destination)) {
                for (unsigned suffix = 1; std::filesystem::exists(destination); ++suffix)
                    destination = paths_.shaders / "Custom" /
                        (source.stem().wstring() + L'-' + std::to_wstring(suffix) + source.extension().wstring());
                std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none);
            }
            imported.push_back(std::filesystem::weakly_canonical(destination));
        }
        engine_.SetShaders(imported);
        shaderPresetIndex_ = shaders_.Presets().size();
    } catch (const std::exception& error) {
        ShowError(L"Custom shaders", error.what());
        engine_.SetShaders({});
        shaderPresetIndex_ = 0;
    }
    InvalidateRect(shader_, nullptr, FALSE);
}

void PlayerWindow::ShowSettingsMenu() {
    ShowChoiceOverlay(L"Playback settings", L"Choose a setting to apply",
                      {L"Network cache  ·  Low latency", L"Network cache  ·  Balanced",
                       L"Network cache  ·  Unstable connection", L"Hardware decoding  ·  Auto",
                       L"Hardware decoding  ·  Off", L"Frame pacing  ·  Display resample",
                       L"Frame pacing  ·  Audio clock"}, -1, OverlayAction::Settings);
}

void PlayerWindow::ShowChoiceOverlay(std::wstring title, std::wstring hint,
                                     std::vector<std::wstring> choices, int selected,
                                     OverlayAction action) {
    if (choices.empty()) return;
    if (sourceSelection_) HideSourceSelector();
    overlayMode_ = OverlayMode::Choice;
    overlayAction_ = action;
    overlayChoices_ = std::move(choices);
    overlaySelection_ = selected >= 0 && static_cast<std::size_t>(selected) < overlayChoices_.size() ? selected : 0;
    SetWindowTextW(overlayTitle_, title.c_str());
    SetWindowTextW(overlayBody_, hint.c_str());
    SetWindowTextW(overlayPrimaryButton_, L"Apply");
    SetWindowTextW(overlaySecondaryButton_, L"Cancel");
    SendMessageW(overlayList_, LB_RESETCONTENT, 0, 0);
    SendMessageW(overlayList_, LB_SETHORIZONTALEXTENT, 0, 0);
    for (const auto& choice : overlayChoices_) AddListText(overlayList_, choice);
    SendMessageW(overlayList_, LB_SETCURSEL, static_cast<WPARAM>(overlaySelection_), 0);
    ShowControls(true);
    LayoutControls();
    UpdateActiveTimer();
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    SetFocus(overlayList_);
}

void PlayerWindow::ShowUrlOverlay() {
    if (sourceSelection_) HideSourceSelector();
    overlayMode_ = OverlayMode::Url;
    overlayAction_ = OverlayAction::None;
    overlayChoices_.clear();
    overlaySelection_ = -1;
    SetWindowTextW(overlayTitle_, L"Open a link");
    SetWindowTextW(overlayBody_, L"Paste a direct media URL or a supported page address");
    SetWindowTextW(overlayPrimaryButton_, L"Open");
    SetWindowTextW(overlaySecondaryButton_, L"Cancel");
    SetWindowTextW(overlayEdit_, L"https://");
    ShowControls(true);
    LayoutControls();
    UpdateActiveTimer();
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    SendMessageW(overlayEdit_, EM_SETSEL, 8, -1);
    SetFocus(overlayEdit_);
}

void PlayerWindow::ShowMessageOverlay(std::wstring title, std::wstring detail) {
    if (sourceSelection_) HideSourceSelector();
    overlayMode_ = OverlayMode::Message;
    overlayAction_ = OverlayAction::None;
    overlayChoices_.clear();
    overlaySelection_ = -1;
    SetWindowTextW(overlayTitle_, title.c_str());
    SetWindowTextW(overlayBody_, detail.c_str());
    SetWindowTextW(overlayPrimaryButton_, L"Close");
    ShowControls(true);
    LayoutControls();
    UpdateActiveTimer();
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    SetFocus(overlayPrimaryButton_);
}

void PlayerWindow::HideOverlay() {
    overlayMode_ = OverlayMode::None;
    overlayAction_ = OverlayAction::None;
    overlayChoices_.clear();
    overlaySelection_ = -1;
    SendMessageW(overlayList_, LB_RESETCONTENT, 0, 0);
    SendMessageW(overlayList_, LB_SETHORIZONTALEXTENT, 0, 0);
    SetWindowTextW(overlayTitle_, L"");
    SetWindowTextW(overlayBody_, L"");
    LayoutControls();
    UpdateActiveTimer();
    SetFocus(window_);
}

void PlayerWindow::ApplyOverlaySelection() {
    if (overlayMode_ == OverlayMode::Message) { HideOverlay(); return; }
    if (overlayMode_ == OverlayMode::Url) {
        const int length = GetWindowTextLengthW(overlayEdit_);
        if (length <= 0) return;
        std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
        GetWindowTextW(overlayEdit_, value.data(), length + 1);
        value.resize(static_cast<std::size_t>(length));
        HideOverlay();
        ResolveUrl(WideToUtf8(value));
        return;
    }
    if (overlayMode_ != OverlayMode::Choice) return;
    const int selected = static_cast<int>(SendMessageW(overlayList_, LB_GETCURSEL, 0, 0));
    if (selected < 0 || static_cast<std::size_t>(selected) >= overlayChoices_.size()) return;
    const auto action = overlayAction_;
    HideOverlay();
    try {
        if (action == OverlayAction::Audio && static_cast<std::size_t>(selected) < audioTrackIds_.size()) {
            audioSelection_ = selected;
            engine_.SetAudioTrack(audioTrackIds_[static_cast<std::size_t>(selected)]);
        } else if (action == OverlayAction::Subtitles && static_cast<std::size_t>(selected) < subtitleTrackIds_.size()) {
            subtitleSelection_ = selected;
            engine_.SetSubtitleTrack(subtitleTrackIds_[static_cast<std::size_t>(selected)]);
        } else if (action == OverlayAction::Video && static_cast<std::size_t>(selected) < videoTrackIds_.size()) {
            videoSelection_ = selected;
            engine_.SetVideoTrack(videoTrackIds_[static_cast<std::size_t>(selected)]);
        } else if (action == OverlayAction::Shaders) {
            ApplyShaderPreset(static_cast<std::size_t>(selected));
        } else if (action == OverlayAction::Settings && selected >= 0 && selected <= 2) {
            const std::string value = selected == 0 ? "low-latency" : selected == 2 ? "unstable" : "balanced";
            engine_.ConfigureCache(value);
            config_.Set("network.cache_mode", value);
            config_.Save(paths_.config / "player.conf");
        } else if (action == OverlayAction::Settings && (selected == 3 || selected == 4)) {
            const bool enabled = selected == 3;
            engine_.SetHardwareDecoding(enabled);
            config_.Set("playback.hwdec", enabled ? "auto" : "no");
            config_.Save(paths_.config / "player.conf");
        } else if (action == OverlayAction::Settings && (selected == 5 || selected == 6)) {
            const std::string value = selected == 5 ? "display-resample" : "audio";
            engine_.SetVideoSync(value);
            config_.Set("playback.video_sync", value);
            config_.Save(paths_.config / "player.conf");
        }
    } catch (const std::exception& error) {
        ShowError(L"Action failed", error.what());
    }
}

void PlayerWindow::OpenFileDialog() {
    std::wstring path(32768, L'\0');
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = window_;
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = L"Media files\0*.mkv;*.mp4;*.m4v;*.mov;*.webm;*.avi;*.ts;*.m2ts;*.mp3;*.flac;*.opus;*.m4a\0All files\0*.*\0";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&dialog)) OpenMedia(WideToUtf8(path.c_str()));
}

void PlayerWindow::OpenUrlDialog() {
    ShowUrlOverlay();
}

void PlayerWindow::ResolveUrl(std::string value, HeaderMap inheritedHeaders) {
    const auto parsed = Url::Parse(value);
    if (!parsed) {
        if (std::filesystem::exists(std::filesystem::path(Utf8ToWide(value)))) OpenMedia(std::move(value));
        else ShowError(L"Open", "The path does not exist or the URL scheme is not allowed");
        return;
    }
    if (sourceSelection_) HideSourceSelector();
    if (overlayMode_ != OverlayMode::None) HideOverlay();
    if (inheritedHeaders.empty()) {
        browserRetryUrl_.clear();
        browserRetryHeaders_.clear();
        browserRetriesRemaining_ = 0;
    }
    if (parsed->IsDirectMedia()) {
        std::vector<std::pair<std::string, std::string>> headers(inheritedHeaders.begin(), inheritedHeaders.end());
        OpenMedia(parsed->Value(), std::move(headers));
        return;
    }
    bool expected = false;
    if (!resolving_.compare_exchange_strong(expected, true)) {
        ShowError(L"URL resolver", "Another URL is already being resolved");
        return;
    }
    SetWindowTextW(window_, L"WannaViewer — resolving URL…");
    resolverThread_ = std::jthread([this, url = *parsed, headers = std::move(inheritedHeaders)](std::stop_token token) {
        ResolveContext context{http_, logger_, token, headers};
        auto result = resolvers_.Resolve(url, context);
        resolving_ = false;
        if (closing_.load()) return;
        auto payload = std::make_unique<ResolveResult>(std::move(result));
        if (PostMessageW(window_, kResolverMessage, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
    });
}

void PlayerWindow::HandleResolveResult(ResolveResult result) {
    SetWindowTextW(window_, L"WannaViewer");
    if (result.status != ResolveStatus::Resolved) { ShowError(L"URL resolver", result.message); return; }
    std::vector<const StreamVariant*> choices;
    for (const auto& season : result.entry.seasons)
        for (const auto& voice : season.voiceTracks)
            for (const auto& episode : voice.episodes)
                for (const auto& stream : episode.streams) choices.push_back(&stream);
    if (choices.empty()) { ShowError(L"URL resolver", "Metadata was found, but no public playable stream is available"); return; }
    if (choices.size() == 1 && !choices.front()->protectedStream) { OpenVariant(*choices.front()); return; }
    ShowSourceSelector(std::move(result));
}

void PlayerWindow::ShowSourceSelector(ResolveResult result) {
    if (overlayMode_ != OverlayMode::None) HideOverlay();
    sourceSelection_ = std::move(result);
    sourceSeasonIndex_ = -1;
    sourceVoiceIndex_ = -1;
    sourceEpisodeIndex_ = -1;
    sourceStreamIndex_ = -1;
    bool foundPlayable = false;
    for (std::size_t seasonIndex = 0; seasonIndex < sourceSelection_->entry.seasons.size() && !foundPlayable; ++seasonIndex) {
        const auto& season = sourceSelection_->entry.seasons[seasonIndex];
        for (std::size_t voiceIndex = 0; voiceIndex < season.voiceTracks.size() && !foundPlayable; ++voiceIndex) {
            const auto& voice = season.voiceTracks[voiceIndex];
            for (std::size_t episodeIndex = 0; episodeIndex < voice.episodes.size() && !foundPlayable; ++episodeIndex) {
                const auto& episode = voice.episodes[episodeIndex];
                for (std::size_t streamIndex = 0; streamIndex < episode.streams.size(); ++streamIndex) {
                    if (episode.streams[streamIndex].protectedStream) continue;
                    sourceSeasonIndex_ = static_cast<int>(seasonIndex);
                    sourceVoiceIndex_ = static_cast<int>(voiceIndex);
                    sourceEpisodeIndex_ = static_cast<int>(episodeIndex);
                    sourceStreamIndex_ = static_cast<int>(streamIndex);
                    foundPlayable = true;
                    break;
                }
            }
        }
    }
    if (!foundPlayable) {
        sourceSeasonIndex_ = sourceSelection_->entry.seasons.empty() ? -1 : 0;
        sourceVoiceIndex_ = 0;
        sourceEpisodeIndex_ = 0;
        sourceStreamIndex_ = 0;
    }
    const auto subtitle = DisplayLabel(sourceSelection_->entry.title, L"Internet video");
    SetWindowTextW(sourceSubtitle_, subtitle.c_str());
    PopulateSourceSeasons();
    PopulateSourceVoices();
    PopulateSourceEpisodes();
    PopulateSourceStreams();
    ShowControls(true);
    LayoutControls();
    UpdateActiveTimer();
    if (sourceSeasonList_ && IsWindowEnabled(sourceSeasonList_)) SetFocus(sourceSeasonList_);
}

void PlayerWindow::HideSourceSelector() {
    sourceSelection_.reset();
    sourceSeasonIndex_ = sourceVoiceIndex_ = sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
    for (HWND list : {sourceSeasonList_, sourceVoiceList_, sourceEpisodeList_, sourceStreamList_}) {
        if (list) {
            SendMessageW(list, LB_RESETCONTENT, 0, 0);
            SendMessageW(list, LB_SETHORIZONTALEXTENT, 0, 0);
        }
    }
    SetWindowTextW(sourceSubtitle_, L"");
    SetWindowTextW(sourceStatus_, L"");
    EnableWindow(sourceOpenButton_, FALSE);
    LayoutControls();
    UpdateActiveTimer();
    SetFocus(window_);
}

void PlayerWindow::PopulateSourceSeasons() {
    SendMessageW(sourceSeasonList_, LB_RESETCONTENT, 0, 0);
    SendMessageW(sourceSeasonList_, LB_SETHORIZONTALEXTENT, 0, 0);
    if (!sourceSelection_) return;
    const auto& seasons = sourceSelection_->entry.seasons;
    for (const auto& season : seasons) AddListText(sourceSeasonList_, DisplayLabel(season.title, L"Default"));
    if (sourceSeasonIndex_ < 0 || static_cast<std::size_t>(sourceSeasonIndex_) >= seasons.size())
        sourceSeasonIndex_ = seasons.empty() ? -1 : 0;
    SendMessageW(sourceSeasonList_, LB_SETCURSEL, static_cast<WPARAM>(sourceSeasonIndex_), 0);
}

void PlayerWindow::PopulateSourceVoices() {
    SendMessageW(sourceVoiceList_, LB_RESETCONTENT, 0, 0);
    SendMessageW(sourceVoiceList_, LB_SETHORIZONTALEXTENT, 0, 0);
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 ||
        static_cast<std::size_t>(sourceSeasonIndex_) >= sourceSelection_->entry.seasons.size()) {
        sourceVoiceIndex_ = -1;
        return;
    }
    const auto& voices = sourceSelection_->entry.seasons[static_cast<std::size_t>(sourceSeasonIndex_)].voiceTracks;
    for (const auto& voice : voices) AddListText(sourceVoiceList_, DisplayLabel(voice.title, L"Default"));
    if (sourceVoiceIndex_ < 0 || static_cast<std::size_t>(sourceVoiceIndex_) >= voices.size()) {
        sourceVoiceIndex_ = voices.empty() ? -1 : 0;
        for (std::size_t index = 0; index < voices.size(); ++index) {
            const bool hasStreams = std::ranges::any_of(voices[index].episodes,
                [](const Episode& episode) { return !episode.streams.empty(); });
            if (hasStreams) { sourceVoiceIndex_ = static_cast<int>(index); break; }
        }
    }
    SendMessageW(sourceVoiceList_, LB_SETCURSEL, static_cast<WPARAM>(sourceVoiceIndex_), 0);
}

void PlayerWindow::PopulateSourceEpisodes() {
    SendMessageW(sourceEpisodeList_, LB_RESETCONTENT, 0, 0);
    SendMessageW(sourceEpisodeList_, LB_SETHORIZONTALEXTENT, 0, 0);
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 || sourceVoiceIndex_ < 0) {
        sourceEpisodeIndex_ = -1;
        return;
    }
    const auto& season = sourceSelection_->entry.seasons[static_cast<std::size_t>(sourceSeasonIndex_)];
    if (static_cast<std::size_t>(sourceVoiceIndex_) >= season.voiceTracks.size()) {
        sourceEpisodeIndex_ = -1;
        return;
    }
    const auto& episodes = season.voiceTracks[static_cast<std::size_t>(sourceVoiceIndex_)].episodes;
    for (const auto& episode : episodes) AddListText(sourceEpisodeList_, DisplayLabel(episode.title, L"Episode"));
    if (sourceEpisodeIndex_ < 0 || static_cast<std::size_t>(sourceEpisodeIndex_) >= episodes.size()) {
        sourceEpisodeIndex_ = episodes.empty() ? -1 : 0;
        for (std::size_t index = 0; index < episodes.size(); ++index) {
            if (!episodes[index].streams.empty()) { sourceEpisodeIndex_ = static_cast<int>(index); break; }
        }
    }
    SendMessageW(sourceEpisodeList_, LB_SETCURSEL, static_cast<WPARAM>(sourceEpisodeIndex_), 0);
}

void PlayerWindow::PopulateSourceStreams() {
    SendMessageW(sourceStreamList_, LB_RESETCONTENT, 0, 0);
    SendMessageW(sourceStreamList_, LB_SETHORIZONTALEXTENT, 0, 0);
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 || sourceVoiceIndex_ < 0 || sourceEpisodeIndex_ < 0) {
        sourceStreamIndex_ = -1;
    } else {
        const auto& season = sourceSelection_->entry.seasons[static_cast<std::size_t>(sourceSeasonIndex_)];
        if (static_cast<std::size_t>(sourceVoiceIndex_) >= season.voiceTracks.size() ||
            static_cast<std::size_t>(sourceEpisodeIndex_) >= season.voiceTracks[static_cast<std::size_t>(sourceVoiceIndex_)].episodes.size()) {
            sourceStreamIndex_ = -1;
        } else {
            const auto& streams = season.voiceTracks[static_cast<std::size_t>(sourceVoiceIndex_)]
                                      .episodes[static_cast<std::size_t>(sourceEpisodeIndex_)].streams;
            for (const auto& stream : streams) {
                auto label = DisplayLabel(stream.quality, L"Auto");
                if (!stream.codec.empty()) label += L"  •  " + DisplayLabel(stream.codec, L"");
                if (!stream.protocol.empty()) label += L"  •  " + DisplayLabel(stream.protocol, L"");
                if (stream.protectedStream) label += L"  •  Protected";
                AddListText(sourceStreamList_, label);
            }
            if (sourceStreamIndex_ < 0 || static_cast<std::size_t>(sourceStreamIndex_) >= streams.size()) {
                sourceStreamIndex_ = streams.empty() ? -1 : 0;
                for (std::size_t index = 0; index < streams.size(); ++index) {
                    if (!streams[index].protectedStream) { sourceStreamIndex_ = static_cast<int>(index); break; }
                }
            }
        }
    }
    SendMessageW(sourceStreamList_, LB_SETCURSEL, static_cast<WPARAM>(sourceStreamIndex_), 0);
    const auto* selected = SelectedSource();
    EnableWindow(sourceOpenButton_, selected && !selected->protectedStream);
    SetWindowTextW(sourceStatus_, !selected ? L"No source is available for this selection"
                   : selected->protectedStream ? L"This source is protected and cannot be opened"
                   : L"Double-click a source or press Enter to open it");
}

const StreamVariant* PlayerWindow::SelectedSource() const {
    if (!sourceSelection_ || sourceSeasonIndex_ < 0 || sourceVoiceIndex_ < 0 ||
        sourceEpisodeIndex_ < 0 || sourceStreamIndex_ < 0) return nullptr;
    const auto& seasons = sourceSelection_->entry.seasons;
    if (static_cast<std::size_t>(sourceSeasonIndex_) >= seasons.size()) return nullptr;
    const auto& voices = seasons[static_cast<std::size_t>(sourceSeasonIndex_)].voiceTracks;
    if (static_cast<std::size_t>(sourceVoiceIndex_) >= voices.size()) return nullptr;
    const auto& episodes = voices[static_cast<std::size_t>(sourceVoiceIndex_)].episodes;
    if (static_cast<std::size_t>(sourceEpisodeIndex_) >= episodes.size()) return nullptr;
    const auto& streams = episodes[static_cast<std::size_t>(sourceEpisodeIndex_)].streams;
    if (static_cast<std::size_t>(sourceStreamIndex_) >= streams.size()) return nullptr;
    return &streams[static_cast<std::size_t>(sourceStreamIndex_)];
}

void PlayerWindow::OpenSelectedSource() {
    const auto* stream = SelectedSource();
    if (!stream) return;
    if (stream->protectedStream) {
        SetWindowTextW(sourceStatus_, L"This source is protected and cannot be opened");
        return;
    }
    const StreamVariant selected = *stream;
    HideSourceSelector();
    OpenVariant(selected);
}

void PlayerWindow::OpenVariant(const StreamVariant& stream) {
    if (stream.protectedStream) { ShowError(L"Provider", "Provider unsupported: protected/DRM stream"); return; }
    if (stream.protocol == "embed") {
        const auto provider = Url::Parse(stream.url);
        if (provider && (provider->HostIs("kodikplayer.com") || provider->HostIs("alloha.yani.tv"))) {
            browserRetryUrl_ = stream.url;
            browserRetryHeaders_ = stream.headers;
            browserRetriesRemaining_ = 1;
        } else {
            browserRetryUrl_.clear();
            browserRetryHeaders_.clear();
            browserRetriesRemaining_ = 0;
        }
        ResolveUrl(stream.url, stream.headers);
        return;
    }
    std::vector<std::pair<std::string, std::string>> headers(stream.headers.begin(), stream.headers.end());
    OpenMedia(stream.url, std::move(headers), stream.audioUrl);
}

bool PlayerWindow::RetryBrowserProvider() {
    if (browserRetriesRemaining_ == 0 || browserRetryUrl_.empty() || resolving_.load()) return false;
    --browserRetriesRemaining_;
    engine_.Stop();
    SetMediaLoaded(false);
    playbackStarted_ = false;
    ResolveUrl(browserRetryUrl_, browserRetryHeaders_);
    return true;
}

void PlayerWindow::HandlePlaybackEvent(PlaybackEvent event) {
    if (event.type == PlaybackEventType::StartFile) {
        playbackStarted_ = false;
        startupTimeoutReported_ = false;
        playbackLoadStarted_ = std::chrono::steady_clock::now();
        SetWindowTextW(window_, L"WannaViewer — opening…");
        UpdateActiveTimer();
    }
    else if (event.type == PlaybackEventType::TracksChanged) UpdateTracks();
    else if (event.type == PlaybackEventType::FileLoaded) {
        SetMediaLoaded(true);
        SetWindowTextW(window_, benchmarkMode_ ? L"WannaViewer — benchmark buffering" : L"WannaViewer — buffering…");
        if (benchmarkMode_ && benchmarkProfile_ == "hardware-shader") ApplyShaderHotkey(2);
    }
    else if (event.type == PlaybackEventType::PlaybackStarted) {
        playbackStarted_ = true;
        browserRetryUrl_.clear();
        browserRetryHeaders_.clear();
        browserRetriesRemaining_ = 0;
        SetWindowTextW(window_, benchmarkMode_ ? L"WannaViewer — benchmark running" : L"WannaViewer — playing");
        if (benchmarkMode_ && !benchmarkRunning_) {
            benchmarkSamples_.clear();
            benchmarkDroppedBaseline_ = -1;
            benchmarkDelayedBaseline_ = -1;
            benchmarkStart_ = std::chrono::steady_clock::now();
            lastBenchmarkSample_ = benchmarkStart_;
            benchmarkRunning_ = true;
        }
        UpdateActiveTimer();
    }
    else if (event.type == PlaybackEventType::EndFile && benchmarkRunning_) FinishBenchmark();
    else if (event.type == PlaybackEventType::Error) {
        if (!playbackStarted_ && RetryBrowserProvider()) return;
        if (!mediaLoaded_) {
            mediaOpening_ = false;
            LayoutControls();
        }
        ShowError(Utf8ToWide(event.name), event.value);
    }
}

void PlayerWindow::FinishBenchmark() {
    benchmarkRunning_ = false;
    if (benchmarkSamples_.empty()) benchmarkSamples_.push_back(engine_.Statistics());
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - benchmarkStart_).count();
    double cpuTotal = 0.0;
    double cpuPeak = 0.0;
    for (const auto& sample : benchmarkSamples_) { cpuTotal += sample.cpuPercent; cpuPeak = std::max(cpuPeak, sample.cpuPercent); }
    const auto& last = benchmarkSamples_.back();
    const auto droppedAfterWarmup = std::max<std::int64_t>(0, last.droppedFrames - std::max<std::int64_t>(0, benchmarkDroppedBaseline_));
    const auto delayedAfterWarmup = std::max<std::int64_t>(0, last.delayedFrames - std::max<std::int64_t>(0, benchmarkDelayedBaseline_));
    nlohmann::json report{
        {"file", benchmarkInput_}, {"profile", benchmarkProfile_}, {"elapsed_seconds", elapsed},
        {"codec", last.videoCodec}, {"resolution", last.resolution}, {"fps", last.fps}, {"pixel_format", last.pixelFormat},
        {"bit_depth", last.bitDepth}, {"hdr", last.hdrStatus}, {"decoder", last.hardwareDecoder},
        {"renderer", last.gpuRenderer}, {"average_cpu_percent", cpuTotal / static_cast<double>(benchmarkSamples_.size())},
        {"peak_cpu_percent", cpuPeak}, {"average_decode_ms", nullptr}, {"average_render_ms", nullptr},
        {"p99_render_ms", nullptr}, {"dropped_frames", droppedAfterWarmup}, {"delayed_frames", delayedAfterWarmup},
        {"dropped_frames_total", last.droppedFrames}, {"delayed_frames_total", last.delayedFrames},
        {"warmup_seconds", 1.0},
        {"shader_configuration", last.shaderChain},
        {"timing_note", "decode/render timing is not exposed by the stable libmpv client API in this build"}
    };
    const auto output = report.dump(2) + "\n";
    {
        std::ofstream file(paths_.root / "benchmark.json", std::ios::trunc);
        file << output;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) || GetLastError() == ERROR_ACCESS_DENIED) {
        const HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
        if (console && console != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            (void)WriteFile(console, output.data(), static_cast<DWORD>(output.size()), &written, nullptr);
        }
    }
    logger_.Write(LogLevel::Info, "benchmark", "Benchmark completed; report written to benchmark.json");
    PostMessageW(window_, WM_CLOSE, 0, 0);
}

void PlayerWindow::ShowError(std::wstring_view title, std::string_view detail) {
    logger_.Write(LogLevel::Error, "ui", detail);
    ShowMessageOverlay(std::wstring(title), Utf8ToWide(detail));
}

void PlayerWindow::LogHardwareInformation() {
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    logger_.Write(LogLevel::Info, "hardware", std::format("Windows architecture={} logical_processors={}",
                  system.wProcessorArchitecture, system.dwNumberOfProcessors));
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 description{};
        if (SUCCEEDED(adapter->GetDesc1(&description)))
            logger_.Write(LogLevel::Info, "hardware", "GPU: " + WideToUtf8(description.Description));
        for (UINT outputIndex = 0;; ++outputIndex) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
            IDXGIOutput6* output6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&output6)))) {
                DXGI_OUTPUT_DESC1 outputDescription{};
                if (SUCCEEDED(output6->GetDesc1(&outputDescription)))
                    logger_.Write(LogLevel::Info, "hardware", std::format("Display={} bits_per_color={} colorspace={} HDR_candidate={}",
                                  WideToUtf8(outputDescription.DeviceName), outputDescription.BitsPerColor,
                                  static_cast<unsigned>(outputDescription.ColorSpace), outputDescription.BitsPerColor >= 10));
                output6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    DEVMODEW mode{sizeof(mode)};
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
        logger_.Write(LogLevel::Info, "hardware", std::format("Primary display {}x{} {}bpp {}Hz",
                      mode.dmPelsWidth, mode.dmPelsHeight, mode.dmBitsPerPel, mode.dmDisplayFrequency));
}

void PlayerWindow::DrawPlayerIcon(HDC dc, UINT id, const RECT& rectangle, bool enabled) {
    const COLORREF color = enabled ? kTextColor : RGB(92, 92, 92);
    const float cx = static_cast<float>(rectangle.left + rectangle.right) / 2.0F;
    const float cy = static_cast<float>(rectangle.top + rectangle.bottom) / 2.0F;
    const float density = static_cast<float>(std::max(1U, dpi_)) / 96.0F;
    const auto s = [density](float value) { return value * density; };
    const float r = s(9.25F);
    bool drawTen = false;
    bool drawHd = false;
    {
        Gdiplus::Graphics graphics(dc);
        ConfigureSmoothGraphics(graphics);
        Gdiplus::Pen pen(SmoothColor(color), std::max(1.25F, s(1.75F)));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        pen.SetLineJoin(Gdiplus::LineJoinRound);
        if (id == kPlay) {
            if (mediaLoaded_ && !engine_.IsPaused()) {
                graphics.DrawLine(&pen, cx - s(3.5F), cy - s(8.5F), cx - s(3.5F), cy + s(8.5F));
                graphics.DrawLine(&pen, cx + s(3.5F), cy - s(8.5F), cx + s(3.5F), cy + s(8.5F));
            } else {
                const Gdiplus::PointF points[]{{cx - s(5.5F), cy - s(8.5F)},
                                               {cx - s(5.5F), cy + s(8.5F)},
                                               {cx + s(7.5F), cy}};
                graphics.DrawPolygon(&pen, points, static_cast<INT>(std::size(points)));
            }
        } else if (id == kRewind || id == kForward) {
            const bool forward = id == kForward;
            graphics.DrawArc(&pen, cx - r, cy - r, r * 2.0F, r * 2.0F,
                             forward ? -120.0F : -60.0F, forward ? 285.0F : -285.0F);
            const float direction = forward ? 1.0F : -1.0F;
            const Gdiplus::PointF arrow[]{{cx + direction * s(3.25F), cy - s(8.25F)},
                                          {cx + direction * s(8.25F), cy - s(5.75F)},
                                          {cx + direction * s(7.5F), cy - s(0.5F)}};
            graphics.DrawLines(&pen, arrow, static_cast<INT>(std::size(arrow)));
            drawTen = true;
        } else if (id == kMute) {
            const Gdiplus::PointF speaker[]{{cx - s(9.5F), cy - s(4.25F)},
                {cx - s(5.0F), cy - s(4.25F)}, {cx - s(0.5F), cy - s(8.5F)},
                {cx - s(0.5F), cy + s(8.5F)}, {cx - s(5.0F), cy + s(4.25F)},
                {cx - s(9.5F), cy + s(4.25F)}, {cx - s(9.5F), cy - s(4.25F)}};
            graphics.DrawLines(&pen, speaker, static_cast<INT>(std::size(speaker)));
            graphics.DrawArc(&pen, cx - s(2.75F), cy - s(5.0F), s(9.0F), s(10.0F), -48.0F, 96.0F);
            graphics.DrawArc(&pen, cx - s(2.0F), cy - s(8.25F), s(15.0F), s(16.5F), -48.0F, 96.0F);
        } else if (id == kAudio) {
            graphics.DrawLine(&pen, cx + s(2.5F), cy - s(8.5F), cx + s(2.5F), cy + s(5.0F));
            graphics.DrawLine(&pen, cx + s(2.5F), cy - s(8.5F), cx + s(8.5F), cy - s(6.25F));
            graphics.DrawEllipse(&pen, cx - s(4.25F), cy + s(2.25F), s(7.25F), s(5.75F));
        } else if (id == kSubtitles) {
            Gdiplus::GraphicsPath path;
            AddRoundedRectangle(path, Gdiplus::RectF(cx - s(11.0F), cy - s(8.0F),
                                                     s(22.0F), s(16.0F)), s(4.0F));
            graphics.DrawPath(&pen, &path);
            for (float offset : {-3.0F, 2.0F}) {
                graphics.DrawLine(&pen, cx - s(7.0F), cy + s(offset), cx - s(1.0F), cy + s(offset));
                graphics.DrawLine(&pen, cx + s(2.0F), cy + s(offset), cx + s(7.0F), cy + s(offset));
            }
        } else if (id == kVideoQuality) {
            Gdiplus::GraphicsPath path;
            AddRoundedRectangle(path, Gdiplus::RectF(cx - s(11.0F), cy - s(8.0F),
                                                     s(22.0F), s(16.0F)), s(4.0F));
            graphics.DrawPath(&pen, &path);
            drawHd = true;
        } else if (id == kShader) {
            const Gdiplus::PointF sparkle[]{{cx, cy - s(10.0F)}, {cx + s(2.0F), cy - s(2.0F)},
                {cx + s(10.0F), cy}, {cx + s(2.0F), cy + s(2.0F)}, {cx, cy + s(10.0F)},
                {cx - s(2.0F), cy + s(2.0F)}, {cx - s(10.0F), cy}, {cx - s(2.0F), cy - s(2.0F)}};
            graphics.DrawPolygon(&pen, sparkle, static_cast<INT>(std::size(sparkle)));
        } else if (id == kStatistics) {
            graphics.DrawLine(&pen, cx - s(10.0F), cy + s(8.5F), cx + s(10.0F), cy + s(8.5F));
            graphics.DrawLine(&pen, cx - s(6.5F), cy + s(7.5F), cx - s(6.5F), cy + s(1.5F));
            graphics.DrawLine(&pen, cx, cy + s(7.5F), cx, cy - s(3.0F));
            graphics.DrawLine(&pen, cx + s(6.5F), cy + s(7.5F), cx + s(6.5F), cy - s(8.5F));
        } else if (id == kSettings) {
            graphics.DrawEllipse(&pen, cx - s(7.25F), cy - s(7.25F), s(14.5F), s(14.5F));
            graphics.DrawEllipse(&pen, cx - s(2.5F), cy - s(2.5F), s(5.0F), s(5.0F));
            for (int i = 0; i < 8; ++i) {
                const double angle = 3.14159265358979323846 * static_cast<double>(i) / 4.0;
                graphics.DrawLine(&pen,
                    cx + static_cast<float>(std::cos(angle)) * s(8.25F),
                    cy + static_cast<float>(std::sin(angle)) * s(8.25F),
                    cx + static_cast<float>(std::cos(angle)) * s(10.75F),
                    cy + static_cast<float>(std::sin(angle)) * s(10.75F));
            }
        } else if (id == kFullscreen) {
            const float outer = s(10.0F);
            const float inner = s(4.5F);
            const Gdiplus::PointF topLeft[]{{cx - outer, cy - inner}, {cx - outer, cy - outer},
                                            {cx - inner, cy - outer}};
            const Gdiplus::PointF topRight[]{{cx + inner, cy - outer}, {cx + outer, cy - outer},
                                             {cx + outer, cy - inner}};
            const Gdiplus::PointF bottomRight[]{{cx + outer, cy + inner}, {cx + outer, cy + outer},
                                                {cx + inner, cy + outer}};
            const Gdiplus::PointF bottomLeft[]{{cx - inner, cy + outer}, {cx - outer, cy + outer},
                                               {cx - outer, cy + inner}};
            graphics.DrawLines(&pen, topLeft, static_cast<INT>(std::size(topLeft)));
            graphics.DrawLines(&pen, topRight, static_cast<INT>(std::size(topRight)));
            graphics.DrawLines(&pen, bottomRight, static_cast<INT>(std::size(bottomRight)));
            graphics.DrawLines(&pen, bottomLeft, static_cast<INT>(std::size(bottomLeft)));
        }
    }
    if (drawTen || drawHd) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        const HGDIOBJ previousFont = SelectObject(dc, iconFont_ ? iconFont_ : font_);
        RECT text{static_cast<LONG>(std::lround(cx - s(8.0F))),
                  static_cast<LONG>(std::lround(cy - s(6.5F))),
                  static_cast<LONG>(std::lround(cx + s(8.0F))),
                  static_cast<LONG>(std::lround(cy + s(7.0F)))};
        DrawTextW(dc, drawTen ? L"10" : L"HD", -1, &text,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (previousFont) SelectObject(dc, previousFont);
    }
}

LRESULT PlayerWindow::DrawControl(DRAWITEMSTRUCT item) {
    if (!item.hDC || !item.hwndItem) return FALSE;
    BufferedDrawSurface surface(item.hDC, item.rcItem);
    item.hDC = surface.Dc();
    RECT rectangle = item.rcItem;
    if (item.CtlType == ODT_LISTBOX && IsStyledListId(item.CtlID)) {
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        FillRect(item.hDC, &rectangle, statisticsBrush_);
        if (selected) {
            RECT selection = rectangle;
            InflateRect(&selection, -Scale(4), -Scale(2));
            FillRoundedRectangle(item.hDC, selection, RGB(32, 32, 32), Scale(16));
        }
        if (item.itemID != static_cast<UINT>(-1)) {
            const auto length = static_cast<int>(SendMessageW(item.hwndItem, LB_GETTEXTLEN, item.itemID, 0));
            if (length >= 0) {
                std::wstring text(static_cast<std::size_t>(length) + 1U, L'\0');
                SendMessageW(item.hwndItem, LB_GETTEXT, item.itemID, reinterpret_cast<LPARAM>(text.data()));
                text.resize(static_cast<std::size_t>(length));
                SetBkMode(item.hDC, TRANSPARENT);
                SetTextColor(item.hDC, kTextColor);
                SelectObject(item.hDC, font_);
                RECT textRect = rectangle;
                textRect.left += selected ? Scale(24) : Scale(12);
                textRect.right -= Scale(8);
                DrawTextW(item.hDC, text.c_str(), static_cast<int>(text.size()), &textRect,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (selected) {
                    const int centerY = (rectangle.top + rectangle.bottom) / 2;
                    const RECT marker{rectangle.left + Scale(10), centerY - Scale(3),
                                      rectangle.left + Scale(16), centerY + Scale(3)};
                    FillSmoothEllipse(item.hDC, marker, kTextColor);
                }
            }
        }
        if ((item.itemState & ODS_FOCUS) != 0) {
            RECT focus = rectangle;
            InflateRect(&focus, -Scale(4), -Scale(3));
            StrokeRoundedRectangle(item.hDC, focus, RGB(86, 86, 86), Scale(14));
        }
        return TRUE;
    }
    if (item.CtlID == kControlsBar) {
        FillRect(item.hDC, &rectangle, backgroundBrush_);
        FillRoundedRectangle(item.hDC, rectangle, kPanelColor, Scale(36));
        return TRUE;
    }
    if (item.CtlID == kSourcePanel || item.CtlID == kOverlayPanel) {
        FillRect(item.hDC, &rectangle, backgroundBrush_);
        RECT card = rectangle;
        InflateRect(&card, -1, -1);
        PaintRoundedRectangle(item.hDC, card, kPanelColor, kBorderColor, Scale(44));
        const auto drawContainer = [&](HWND control) {
            const auto bounds = RelativeControlRectangle(control, item.hwndItem, Scale(4));
            if (bounds) PaintRoundedRectangle(item.hDC, *bounds, kStatisticsColor, kBorderColor, Scale(24));
        };
        if (item.CtlID == kSourcePanel) {
            for (HWND list : {sourceSeasonList_, sourceVoiceList_, sourceEpisodeList_, sourceStreamList_})
                drawContainer(list);
        } else if (overlayMode_ == OverlayMode::Url) {
            drawContainer(overlayEdit_);
        } else if (overlayMode_ == OverlayMode::Choice) {
            drawContainer(overlayList_);
        }
        return TRUE;
    }
    if (item.CtlID == kEmptyState) {
        FillRect(item.hDC, &rectangle, backgroundBrush_);
        RECT card = rectangle;
        InflateRect(&card, -1, -1);
        PaintRoundedRectangle(item.hDC, card, kPanelColor, kBorderColor, Scale(40));
        const int cx = (rectangle.left + rectangle.right) / 2;
        const int iconY = rectangle.top + Scale(56);
        {
            Gdiplus::Graphics graphics(item.hDC);
            ConfigureSmoothGraphics(graphics);
            Gdiplus::Pen pen(SmoothColor(kTextColor), static_cast<float>(std::max(1, Scale(2))));
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            graphics.DrawEllipse(&pen, static_cast<float>(cx - Scale(28)),
                                  static_cast<float>(iconY - Scale(28)),
                                  static_cast<float>(Scale(56)), static_cast<float>(Scale(56)));
            const Gdiplus::PointF play[]{{static_cast<float>(cx - Scale(7)), static_cast<float>(iconY - Scale(12))},
                                         {static_cast<float>(cx - Scale(7)), static_cast<float>(iconY + Scale(12))},
                                         {static_cast<float>(cx + Scale(12)), static_cast<float>(iconY)}};
            graphics.DrawPolygon(&pen, play, static_cast<INT>(std::size(play)));
        }
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, kTextColor);
        SelectObject(item.hDC, titleFont_);
        RECT title{rectangle.left, rectangle.top + Scale(96), rectangle.right, rectangle.top + Scale(134)};
        DrawTextW(item.hDC, L"Open a video", -1, &title, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(item.hDC, font_);
        SetTextColor(item.hDC, kMutedTextColor);
        RECT hint{rectangle.left + Scale(16), rectangle.top + Scale(137), rectangle.right - Scale(16), rectangle.top + Scale(166)};
        DrawTextW(item.hDC, L"Drop a media file here, or open a local file or link", -1, &hint,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }

    const bool enabled = (item.itemState & ODS_DISABLED) == 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = GetPropW(item.hwndItem, kHoverProperty) != nullptr;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const bool chrome = item.CtlID == kPlay || item.CtlID == kRewind || item.CtlID == kForward ||
                        item.CtlID == kMute || item.CtlID == kAudio || item.CtlID == kSubtitles ||
                        item.CtlID == kVideoQuality || item.CtlID == kShader || item.CtlID == kStatistics ||
                        item.CtlID == kSettings || item.CtlID == kFullscreen;
    const bool active = (item.CtlID == kStatistics && statisticsVisible_) ||
                        (item.CtlID == kFullscreen && fullscreen_) ||
                        (item.CtlID == kSubtitles && subtitleSelection_ > 0);
    if (chrome) {
        FillRect(item.hDC, &rectangle, panelBrush_);
        if (hot || pressed || active || focused) {
            RECT hover = rectangle; InflateRect(&hover, -Scale(3), -Scale(3));
            const int diameter = std::min(hover.right - hover.left, hover.bottom - hover.top);
            FillRoundedRectangle(item.hDC, hover,
                                 pressed ? kButtonPressedColor : kButtonHotColor, diameter);
        }
        DrawPlayerIcon(item.hDC, item.CtlID, rectangle, enabled);
    } else {
        FillRect(item.hDC, &rectangle, panelBrush_);
        RECT button = rectangle; InflateRect(&button, -1, -1);
        const COLORREF fillColor = pressed ? kButtonPressedColor : hot ? kButtonHotColor : kPanelColor;
        const int diameter = std::min(button.right - button.left, button.bottom - button.top);
        PaintRoundedRectangle(item.hDC, button, fillColor,
                              enabled ? (hot ? kTextColor : kBorderColor) : RGB(32, 32, 32),
                              diameter);
        wchar_t text[128]{};
        GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, enabled ? kTextColor : RGB(92, 92, 92));
        SelectObject(item.hDC, font_);
        RECT textRect = button; InflateRect(&textRect, -Scale(8), 0);
        DrawTextW(item.hDC, text, -1, &textRect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    if (focused && !chrome) {
        RECT focus = rectangle;
        InflateRect(&focus, -Scale(5), -Scale(5));
        const int diameter = std::min(focus.right - focus.left, focus.bottom - focus.top);
        StrokeRoundedRectangle(item.hDC, focus, RGB(86, 86, 86), diameter);
    }
    return TRUE;
}

LRESULT PlayerWindow::DrawTrackbar(NMCUSTOMDRAW customDraw) {
    if (customDraw.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    RECT client{};
    GetClientRect(customDraw.hdr.hwndFrom, &client);
    BufferedDrawSurface surface(customDraw.hdc, client);
    customDraw.hdc = surface.Dc();
    FillRect(customDraw.hdc, &client, panelBrush_);
    const bool timeline = customDraw.hdr.hwndFrom == timeline_;
    const bool enabled = IsWindowEnabled(customDraw.hdr.hwndFrom) != FALSE;
    const int minimum = static_cast<int>(SendMessageW(customDraw.hdr.hwndFrom, TBM_GETRANGEMIN, 0, 0));
    const int maximum = static_cast<int>(SendMessageW(customDraw.hdr.hwndFrom, TBM_GETRANGEMAX, 0, 0));
    const int position = static_cast<int>(SendMessageW(customDraw.hdr.hwndFrom, TBM_GETPOS, 0, 0));
    const double interaction = timeline ? std::clamp(timelineAnimationProgress_, 0.0, 1.0) : 0.0;
    const int margin = Scale(timeline ? kTimelineChannelInset : 5);
    const int centerY = timeline ? client.bottom - Scale(9) : (client.top + client.bottom) / 2;
    RECT channel{client.left + margin, centerY - Scale(timeline ? 2 : 1),
                 client.right - margin, centerY + Scale(2)};
    FillRoundedRectangle(customDraw.hdc, channel,
                         timeline ? BlendColor(RGB(48, 48, 48), RGB(66, 66, 66), interaction)
                                  : RGB(48, 48, 48),
                         Scale(4));
    const double ratio = maximum > minimum
        ? std::clamp(static_cast<double>(position - minimum) / static_cast<double>(maximum - minimum), 0.0, 1.0)
        : 0.0;
    RECT progress = channel;
    progress.right = progress.left + static_cast<int>(static_cast<double>(progress.right - progress.left) * ratio);
    if (progress.right > progress.left)
        FillRoundedRectangle(customDraw.hdc, progress, enabled ? kTextColor : RGB(82, 82, 82), Scale(4));
    const int thumbX = std::clamp(progress.right, channel.left, channel.right);
    const float thumbRadius = timeline
        ? static_cast<float>(Scale(5)) + static_cast<float>(Scale(2)) * static_cast<float>(interaction)
        : static_cast<float>(Scale(4));
    FillSmoothEllipse(customDraw.hdc, static_cast<float>(thumbX) - thumbRadius,
                      static_cast<float>(centerY) - thumbRadius, thumbRadius * 2.0F,
                      thumbRadius * 2.0F, enabled ? kTextColor : RGB(82, 82, 82));
    if (timeline && interaction > 0.01 && engine_.Duration() > 0.0) {
        const int channelLeft = static_cast<int>(channel.left);
        const int channelRight = static_cast<int>(channel.right);
        const int hoverX = std::clamp(timelineHoverX_, channelLeft, channelRight);
        const double hoverRatio = static_cast<double>(hoverX - channelLeft) /
                                  static_cast<double>(std::max(1, channelRight - channelLeft));
        const auto label = TimeText(engine_.Duration() * hoverRatio);
        const int bubbleWidth = Scale(72);
        const int bubbleOffset = static_cast<int>(std::lround(
            static_cast<double>(Scale(5)) * (1.0 - interaction)));
        RECT bubble{std::clamp(hoverX - bubbleWidth / 2, static_cast<int>(client.left),
                               static_cast<int>(client.right) - bubbleWidth),
                    client.top + bubbleOffset, 0, Scale(29) + bubbleOffset};
        bubble.right = bubble.left + bubbleWidth;
        PaintRoundedRectangle(customDraw.hdc, bubble, RGB(7, 7, 7), kBorderColor, Scale(12));
        {
            Gdiplus::Graphics graphics(customDraw.hdc);
            ConfigureSmoothGraphics(graphics);
            Gdiplus::SolidBrush arrowFill(SmoothColor(RGB(7, 7, 7)));
            Gdiplus::Pen arrowBorder(SmoothColor(kBorderColor), 1.0F);
            const Gdiplus::PointF arrow[]{{static_cast<float>(hoverX - Scale(5)), static_cast<float>(bubble.bottom - 1)},
                                          {static_cast<float>(hoverX + Scale(5)), static_cast<float>(bubble.bottom - 1)},
                                          {static_cast<float>(hoverX), static_cast<float>(bubble.bottom + Scale(5))}};
            graphics.FillPolygon(&arrowFill, arrow, static_cast<INT>(std::size(arrow)));
            graphics.DrawLines(&arrowBorder, arrow, static_cast<INT>(std::size(arrow)));
        }
        SetBkMode(customDraw.hdc, TRANSPARENT); SetTextColor(customDraw.hdc, kTextColor);
        const HGDIOBJ previousFont = SelectObject(customDraw.hdc, font_);
        DrawTextW(customDraw.hdc, label.c_str(), -1, &bubble, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (previousFont) SelectObject(customDraw.hdc, previousFont);
    }
    return CDRF_SKIPDEFAULT;
}

LRESULT PlayerWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = Scale(780);
        limits->ptMinTrackSize.y = Scale(500);
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wParam);
        const auto* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        CreateFonts();
        SendMessageW(overlayEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(Scale(10), Scale(10)));
        if (tooltip_) SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, Scale(360));
        LayoutControls();
        return 0;
    }
    case WM_SIZE:
        LayoutControls();
        return 0;
    case WM_MOUSEMOVE:
        RecordMouseMovement();
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
        RecordInteraction();
        return 0;
    case kInteractionMessage:
        if (wParam != FALSE)
            RecordMouseMovement();
        else
            RecordInteraction();
        return 0;
    case kTimelineHoverMessage: {
        const bool hovering = wParam != FALSE;
        const int hoverX = hovering ? static_cast<int>(lParam) : timelineHoverX_;
        const bool hoverPositionChanged = hovering && hoverX != timelineHoverX_;
        timelineHovering_ = hovering;
        timelineHoverX_ = hoverX;
        SetTimelineAnimationTarget(timelineHovering_ || timelineDragging_, GetTickCount64());
        if (hoverPositionChanged) InvalidateRect(timeline_, nullptr, FALSE);
        return 0;
    }
    case kTimelineSeekMessage:
        switch (static_cast<TimelineInput>(wParam)) {
        case TimelineInput::Begin:
        case TimelineInput::Update:
            UpdateTimelineFromPoint(static_cast<int>(lParam), false);
            break;
        case TimelineInput::Commit:
            UpdateTimelineFromPoint(static_cast<int>(lParam), true);
            break;
        case TimelineInput::Cancel:
            CancelTimelineDrag();
            break;
        }
        return 0;
    case WM_TIMER:
        if (wParam == kUiTimer) {
            const ULONGLONG now = GetTickCount64();
            UpdateControlsAnimation(now);
            UpdateTimelineAnimation(now);
            UpdateUi();
            if (!ControlsAnimationActive() && !TimelineAnimationActive() &&
                mediaLoaded_ && controlsVisible_ &&
                !sourceSelection_ && overlayMode_ == OverlayMode::None &&
                !timelineDragging_ && !IsCursorOverControls() &&
                now - lastInteraction_ >= kControlsHideDelayMs)
                ShowControls(false);
            if (!ControlsAnimationActive() && !TimelineAnimationActive()) UpdateActiveTimer();
        }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int notification = HIWORD(wParam);
        if (id == kOverlayList && notification == LBN_SELCHANGE && overlayMode_ == OverlayMode::Choice) {
            overlaySelection_ = static_cast<int>(SendMessageW(overlayList_, LB_GETCURSEL, 0, 0));
        } else if (id == kOverlayList && notification == LBN_DBLCLK && overlayMode_ == OverlayMode::Choice) {
            ApplyOverlaySelection();
        } else if (id == kOverlayPrimary && notification == BN_CLICKED && overlayMode_ != OverlayMode::None) {
            ApplyOverlaySelection();
        } else if (id == kOverlaySecondary && notification == BN_CLICKED && overlayMode_ != OverlayMode::None) {
            HideOverlay();
        } else if (id == kSourceSeasonList && notification == LBN_SELCHANGE && sourceSelection_) {
            sourceSeasonIndex_ = static_cast<int>(SendMessageW(sourceSeasonList_, LB_GETCURSEL, 0, 0));
            sourceVoiceIndex_ = sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
            PopulateSourceVoices();
            PopulateSourceEpisodes();
            PopulateSourceStreams();
        } else if (id == kSourceVoiceList && notification == LBN_SELCHANGE && sourceSelection_) {
            sourceVoiceIndex_ = static_cast<int>(SendMessageW(sourceVoiceList_, LB_GETCURSEL, 0, 0));
            sourceEpisodeIndex_ = sourceStreamIndex_ = -1;
            PopulateSourceEpisodes();
            PopulateSourceStreams();
        } else if (id == kSourceEpisodeList && notification == LBN_SELCHANGE && sourceSelection_) {
            sourceEpisodeIndex_ = static_cast<int>(SendMessageW(sourceEpisodeList_, LB_GETCURSEL, 0, 0));
            sourceStreamIndex_ = -1;
            PopulateSourceStreams();
        } else if (id == kSourceStreamList && notification == LBN_SELCHANGE && sourceSelection_) {
            sourceStreamIndex_ = static_cast<int>(SendMessageW(sourceStreamList_, LB_GETCURSEL, 0, 0));
            const auto* selected = SelectedSource();
            EnableWindow(sourceOpenButton_, selected && !selected->protectedStream);
            SetWindowTextW(sourceStatus_, !selected ? L"No source is available for this selection"
                           : selected->protectedStream ? L"This source is protected and cannot be opened"
                           : L"Double-click a source or press Enter to open it");
        } else if (id == kSourceStreamList && notification == LBN_DBLCLK && sourceSelection_) {
            sourceStreamIndex_ = static_cast<int>(SendMessageW(sourceStreamList_, LB_GETCURSEL, 0, 0));
            OpenSelectedSource();
        } else if (id == kSourceOpen && notification == BN_CLICKED && sourceSelection_) OpenSelectedSource();
        else if (id == kSourceCancel && notification == BN_CLICKED && sourceSelection_) HideSourceSelector();
        else if (id == kPlay && notification == BN_CLICKED) { engine_.TogglePause(); InvalidateRect(playButton_, nullptr, FALSE); }
        else if (id == kRewind && notification == BN_CLICKED) engine_.SeekRelative(-10.0);
        else if (id == kForward && notification == BN_CLICKED) engine_.SeekRelative(10.0);
        else if (id == kMute && HIWORD(wParam) == BN_CLICKED) engine_.ToggleMute();
        else if (id == kStatistics && HIWORD(wParam) == BN_CLICKED) ToggleStatistics();
        else if (id == kOpenFile && HIWORD(wParam) == BN_CLICKED) OpenFileDialog();
        else if (id == kOpenUrl && HIWORD(wParam) == BN_CLICKED) OpenUrlDialog();
        else if (id == kFullscreen && HIWORD(wParam) == BN_CLICKED) ToggleFullscreen();
        else if (id == kSettings && HIWORD(wParam) == BN_CLICKED) ShowSettingsMenu();
        else if (id == kAudio && HIWORD(wParam) == BN_CLICKED) ShowAudioMenu();
        else if (id == kSubtitles && HIWORD(wParam) == BN_CLICKED) ShowSubtitleMenu();
        else if (id == kShader && HIWORD(wParam) == BN_CLICKED) ShowShaderMenu();
        else if (id == kVideoQuality && HIWORD(wParam) == BN_CLICKED) ShowVideoMenu();
        RecordInteraction();
        return 0;
    }
    case WM_DRAWITEM:
        if (lParam) return DrawControl(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        break;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (measure && measure->CtlType == ODT_LISTBOX && IsStyledListId(measure->CtlID)) {
            measure->itemHeight = static_cast<UINT>(Scale(measure->CtlID == kOverlayList ? 38 : 34));
            return TRUE;
        }
        break;
    }
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header && header->code == NM_CUSTOMDRAW && (header->hwndFrom == timeline_ || header->hwndFrom == volume_))
            return DrawTrackbar(*reinterpret_cast<NMCUSTOMDRAW*>(lParam));
        break;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == timeline_) {
            const auto code = LOWORD(wParam);
            const auto position = static_cast<int>(SendMessageW(timeline_, TBM_GETPOS, 0, 0));
            UpdateTimelineFromValue(position, code != TB_THUMBTRACK);
            return 0;
        } else if (reinterpret_cast<HWND>(lParam) == volume_) {
            engine_.SetVolume(static_cast<double>(SendMessageW(volume_, TBM_GETPOS, 0, 0)));
        }
        RecordInteraction();
        return 0;
    case WM_KEYDOWN: {
        RecordInteraction();
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wParam == VK_ESCAPE && overlayMode_ != OverlayMode::None) HideOverlay();
        else if (wParam == VK_ESCAPE && sourceSelection_) HideSourceSelector();
        else if (control && wParam >= '0' && wParam <= '9') ApplyShaderHotkey(static_cast<int>(wParam - '0'));
        else if (control && wParam == 'O') OpenFileDialog();
        else if (control && wParam == 'U') OpenUrlDialog();
        else if (wParam == VK_SPACE) engine_.TogglePause();
        else if (wParam == VK_LEFT) engine_.SeekRelative(shift ? -30.0 : -5.0);
        else if (wParam == VK_RIGHT) engine_.SeekRelative(shift ? 30.0 : 5.0);
        else if (wParam == VK_OEM_PERIOD) engine_.FrameStep();
        else if (wParam == VK_PRIOR) engine_.ChangeChapter(-1);
        else if (wParam == VK_NEXT) engine_.ChangeChapter(1);
        else if (wParam == 'F') ToggleFullscreen();
        else if (wParam == 'M') engine_.ToggleMute();
        else if (wParam == 'S') engine_.CycleSubtitles();
        else if (wParam == 'A') engine_.CycleAudio();
        else if (wParam == VK_F10) ToggleStatistics();
        else if (wParam == VK_ESCAPE && fullscreen_) ToggleFullscreen();
        return 0;
    }
    case WM_DROPFILES: {
        const HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring path(static_cast<std::size_t>(length) + 1U, L'\0');
        DragQueryFileW(drop, 0, path.data(), length + 1);
        path.resize(static_cast<std::size_t>(length));
        DragFinish(drop);
        auto extension = std::filesystem::path(path).extension().wstring();
        std::ranges::transform(extension, extension.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
        if (extension == L".ass" || extension == L".ssa" || extension == L".srt" ||
            extension == L".vtt" || extension == L".sup") engine_.AddSubtitle(WideToUtf8(path));
        else OpenMedia(WideToUtf8(path));
        return 0;
    }
    case kPlaybackMessage: {
        std::unique_ptr<PlaybackEvent> event(reinterpret_cast<PlaybackEvent*>(lParam));
        if (event) HandlePlaybackEvent(std::move(*event));
        return 0;
    }
    case kResolverMessage: {
        std::unique_ptr<ResolveResult> result(reinterpret_cast<ResolveResult*>(lParam));
        if (result) HandleResolveResult(std::move(*result));
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (control == video_) {
            SetBkColor(dc, kWindowColor);
            return reinterpret_cast<LRESULT>(backgroundBrush_);
        }
        if (control == sourceTitle_ || control == sourceSubtitle_ || control == sourceSeasonLabel_ ||
            control == sourceVoiceLabel_ || control == sourceEpisodeLabel_ || control == sourceStreamLabel_ ||
            control == sourceStatus_ || control == overlayTitle_ || control == overlayBody_) {
            SetTextColor(dc, control == sourceSubtitle_ || control == sourceStatus_ || control == overlayBody_
                             ? kMutedTextColor : kTextColor);
            SetBkColor(dc, kPanelColor);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(panelBrush_);
        }
        if (control == timeLabel_ || control == stats_) {
            SetTextColor(dc, control == stats_ ? RGB(210, 221, 234) : kTextColor);
            SetBkColor(dc, control == stats_ ? kStatisticsColor : kPanelColor);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(control == stats_ ? statisticsBrush_ : panelBrush_);
        }
        break;
    }
    case WM_CTLCOLORLISTBOX: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        if (control == sourceSeasonList_ || control == sourceVoiceList_ ||
            control == sourceEpisodeList_ || control == sourceStreamList_ || control == overlayList_) {
            SetTextColor(dc, kTextColor);
            SetBkColor(dc, kStatisticsColor);
            return reinterpret_cast<LRESULT>(statisticsBrush_);
        }
        break;
    }
    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wParam);
        if (reinterpret_cast<HWND>(lParam) == overlayEdit_) {
            SetTextColor(dc, kTextColor);
            SetBkColor(dc, kStatisticsColor);
            return reinterpret_cast<LRESULT>(statisticsBrush_);
        }
        break;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window_, &client);
        FillRect(reinterpret_cast<HDC>(wParam), &client,
                 backgroundBrush_ ? backgroundBrush_ : static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        return 1;
    }
    case WM_CLOSE:
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        closing_ = true;
        if (resolverThread_.joinable()) resolverThread_.request_stop();
        engine_.Shutdown();
        KillTimer(window_, kUiTimer);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

} // namespace wannaviewer
