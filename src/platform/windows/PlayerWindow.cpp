#include "wannaviewer/platform/windows/PlayerWindow.hpp"

#include "wannaviewer/resolvers/DirectMediaResolver.hpp"
#include "wannaviewer/resolvers/GenericResolver.hpp"
#include "wannaviewer/resolvers/YummyAnimeResolver.hpp"

#include <algorithm>
#include <array>
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
#include <shellapi.h>
#include <windowsx.h>

#include <nlohmann/json.hpp>

namespace wannaviewer {
namespace {

constexpr UINT kPlaybackMessage = WM_APP + 1;
constexpr UINT kResolverMessage = WM_APP + 2;
constexpr UINT_PTR kUiTimer = 1;
constexpr int kPlay = 100;
constexpr int kTimeline = 101;
constexpr int kVolume = 102;
constexpr int kAudio = 103;
constexpr int kSubtitles = 104;
constexpr int kShader = 105;
constexpr int kFullscreen = 106;
constexpr int kSettings = 107;
constexpr int kVideoQuality = 108;

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return L"<invalid UTF-8>";
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    (void)MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
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

struct InputDialogState final {
    HWND edit{nullptr};
    bool accepted{false};
    std::wstring value;
};

LRESULT CALLBACK InputDialogProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<InputDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        state = static_cast<InputDialogState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_CREATE:
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"https://", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                      12, 14, 516, 25, window, reinterpret_cast<HMENU>(1), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Open", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                        350, 51, 84, 28, window, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                        444, 51, 84, 28, window, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SendMessageW(state->edit, EM_SETSEL, 8, -1);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            const int length = GetWindowTextLengthW(state->edit);
            state->value.resize(static_cast<std::size_t>(length) + 1U);
            GetWindowTextW(state->edit, state->value.data(), length + 1);
            state->value.resize(static_cast<std::size_t>(length));
            state->accepted = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { DestroyWindow(window); return 0; }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::optional<std::wstring> PromptForUrl(HWND owner, HINSTANCE instance) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = instance;
        type.lpfnWndProc = InputDialogProcedure;
        type.lpszClassName = L"WannaViewer.UrlDialog";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return std::nullopt;
        registered = true;
    }
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + std::max(0L, (ownerRect.right - ownerRect.left - 560L) / 2L);
    const int y = ownerRect.top + std::max(0L, (ownerRect.bottom - ownerRect.top - 130L) / 2L);
    InputDialogState state;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, L"WannaViewer.UrlDialog", L"Open URL",
                                  WS_CAPTION | WS_SYSMENU, x, y, 560, 130, owner, nullptr, instance, &state);
    if (!dialog) return std::nullopt;
    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    SetFocus(state.edit);
    MSG message{};
    bool sawQuit = false;
    int quitCode = 0;
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (message.message == WM_QUIT) { sawQuit = true; quitCode = static_cast<int>(message.wParam); }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (sawQuit) PostQuitMessage(quitCode);
    return state.accepted ? std::optional(state.value) : std::nullopt;
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

} // namespace

PlayerWindow::PlayerWindow(AppPaths paths, Config config, Logger& logger)
    : paths_(std::move(paths)), config_(std::move(config)), logger_(logger),
      shaders_(paths_.shaders, paths_.presets / "shaders.json"),
      ytDlp_(paths_.tools / "yt-dlp.exe"), resolvers_(&ytDlp_), engine_(paths_, config_, logger_) {
    shaders_.Reload();
    resolvers_.Add(std::make_unique<DirectMediaResolver>());
    resolvers_.Add(std::make_unique<YummyAnimeResolver>());
    resolvers_.Add(std::make_unique<GenericResolver>());
}

PlayerWindow::~PlayerWindow() {
    closing_ = true;
    if (resolverThread_.joinable()) { resolverThread_.request_stop(); resolverThread_.join(); }
    engine_.Shutdown();
    if (font_) DeleteObject(font_);
}

void PlayerWindow::Create(HINSTANCE instance, int showCommand) {
    instance_ = instance;
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
    window_ = CreateWindowExW(0, type.lpszClassName, L"WannaViewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1280, 760, nullptr, nullptr, instance, this);
    if (!window_) throw std::runtime_error("Unable to create the player window");
    CreateControls();
    DragAcceptFiles(window_, TRUE);
    engine_.Initialize(reinterpret_cast<std::uintptr_t>(video_), [this](PlaybackEvent event) {
        if (closing_.load()) return;
        auto payload = std::make_unique<PlaybackEvent>(std::move(event));
        if (PostMessageW(window_, kPlaybackMessage, 0, reinterpret_cast<LPARAM>(payload.get()))) payload.release();
    });
    LogHardwareInformation();
    ShowWindow(window_, showCommand);
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
    video_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
                             0, 0, 100, 100, window_, nullptr, instance_, nullptr);
    SetWindowSubclass(video_, VideoSubclass, 1, reinterpret_cast<DWORD_PTR>(window_));
    controlsBar_ = CreateWindowExW(0, L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_BLACKRECT,
                                   0, 0, 100, 52, window_, nullptr, instance_, nullptr);
    playButton_ = CreateWindowExW(0, L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  0, 0, 64, 28, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPlay)), instance_, nullptr);
    timeline_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                                0, 0, 200, 24, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kTimeline)), instance_, nullptr);
    SendMessageW(timeline_, TBM_SETRANGE, TRUE, MAKELONG(0, 10000));
    timeLabel_ = CreateWindowExW(0, L"STATIC", L"00:00 / 00:00", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
                                 0, 0, 112, 28, window_, nullptr, instance_, nullptr);
    volume_ = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
                              0, 0, 80, 24, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVolume)), instance_, nullptr);
    SendMessageW(volume_, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
    SendMessageW(volume_, TBM_SETPOS, TRUE, 80);
    audio_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                             0, 0, 120, 300, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAudio)), instance_, nullptr);
    subtitles_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                 0, 0, 120, 300, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSubtitles)), instance_, nullptr);
    videoQuality_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                    0, 0, 110, 300, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kVideoQuality)), instance_, nullptr);
    shader_ = CreateWindowExW(0, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                              0, 0, 140, 300, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kShader)), instance_, nullptr);
    for (const auto& preset : shaders_.Presets()) SendMessageW(shader_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(preset.name).c_str()));
    SendMessageW(shader_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Custom…"));
    SendMessageW(shader_, CB_SETCURSEL, 0, 0);
    fullscreenButton_ = CreateWindowExW(0, L"BUTTON", L"Full", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                        0, 0, 56, 28, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFullscreen)), instance_, nullptr);
    settingsButton_ = CreateWindowExW(0, L"BUTTON", L"Prefs", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      0, 0, 56, 28, window_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSettings)), instance_, nullptr);
    stats_ = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", L"", WS_CHILD | SS_LEFT,
                             16, 16, 520, 270, window_, nullptr, instance_, nullptr);
    font_ = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (HWND control : {playButton_, timeLabel_, audio_, subtitles_, videoQuality_, shader_, fullscreenButton_, settingsButton_, stats_})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    LayoutControls();
}

void PlayerWindow::LayoutControls() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int barHeight = 52;
    MoveWindow(video_, 0, 0, width, height, TRUE);
    MoveWindow(controlsBar_, 0, height - barHeight, width, barHeight, TRUE);
    int x = 10;
    const int y = height - 40;
    MoveWindow(playButton_, x, y, 64, 28, TRUE); x += 70;
    const int fixedWidth = 112 + 84 + 126 + 126 + 116 + 146 + 62 + 62 + 40;
    const int timelineWidth = std::max(100, width - x - fixedWidth);
    MoveWindow(timeline_, x, y + 2, timelineWidth, 24, TRUE); x += timelineWidth + 6;
    MoveWindow(timeLabel_, x, y, 112, 28, TRUE); x += 118;
    MoveWindow(volume_, x, y + 2, 78, 24, TRUE); x += 84;
    MoveWindow(audio_, x, y, 120, 240, TRUE); x += 126;
    MoveWindow(subtitles_, x, y, 120, 240, TRUE); x += 126;
    MoveWindow(videoQuality_, x, y, 110, 240, TRUE); x += 116;
    MoveWindow(shader_, x, y, 140, 240, TRUE); x += 146;
    MoveWindow(fullscreenButton_, x, y, 56, 28, TRUE); x += 62;
    MoveWindow(settingsButton_, x, y, 56, 28, TRUE);
    SetWindowPos(controlsBar_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (HWND control : {playButton_, timeline_, timeLabel_, volume_, audio_, subtitles_, videoQuality_, shader_, fullscreenButton_, settingsButton_, stats_})
        SetWindowPos(control, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void PlayerWindow::ShowControls(bool show) {
    if (controlsVisible_ == show) return;
    controlsVisible_ = show;
    const int command = show ? SW_SHOWNA : SW_HIDE;
    for (HWND control : {controlsBar_, playButton_, timeline_, timeLabel_, volume_, audio_, subtitles_, videoQuality_, shader_, fullscreenButton_, settingsButton_})
        ShowWindow(control, command);
    if (!show) SetFocus(window_);
    UpdateActiveTimer();
}

void PlayerWindow::RecordInteraction() {
    lastInteraction_ = GetTickCount64();
    ShowControls(true);
    UpdateActiveTimer();
}

void PlayerWindow::UpdateActiveTimer() {
    if (controlsVisible_ || statisticsVisible_ || benchmarkMode_) SetTimer(window_, kUiTimer, 250, nullptr);
    else KillTimer(window_, kUiTimer);
}

void PlayerWindow::UpdateUi() {
    const double duration = engine_.Duration();
    const double position = engine_.Position();
    if (controlsVisible_ && !timelineDragging_) {
        const int trackPosition = duration > 0.0 ? static_cast<int>(std::clamp(position / duration, 0.0, 1.0) * 10000.0) : 0;
        SendMessageW(timeline_, TBM_SETPOS, TRUE, trackPosition);
        const auto label = TimeText(position) + L" / " + TimeText(duration);
        SetWindowTextW(timeLabel_, label.c_str());
        SetWindowTextW(playButton_, engine_.IsPaused() ? L"Play" : L"Pause");
    }
    if (statisticsVisible_ && (++timerTick_ % 2U) == 0) {
        const auto text = Utf8ToWide(engine_.Statistics().ToDisplayText());
        SetWindowTextW(stats_, text.c_str());
    }
    if (benchmarkRunning_ && (++benchmarkTick_ % 4U) == 0) {
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
    SendMessageW(audio_, CB_RESETCONTENT, 0, 0);
    SendMessageW(subtitles_, CB_RESETCONTENT, 0, 0);
    SendMessageW(videoQuality_, CB_RESETCONTENT, 0, 0);
    audioTrackIds_.clear();
    subtitleTrackIds_.clear();
    videoTrackIds_.clear();
    SendMessageW(subtitles_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Subtitles off"));
    subtitleTrackIds_.push_back(-1);
    int audioSelection = -1;
    int subtitleSelection = 0;
    int videoSelection = -1;
    for (const auto& track : engine_.Tracks()) {
        auto label = track.title.empty() ? track.language : track.title;
        if (label.empty() && track.type == "video" && track.height > 0)
            label = std::format("{}p {}", track.height, track.codec);
        if (label.empty()) label = std::format("{} {}", track.type, track.id);
        if (track.type == "audio") {
            SendMessageW(audio_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(label).c_str()));
            audioTrackIds_.push_back(track.id);
            if (track.selected) audioSelection = static_cast<int>(audioTrackIds_.size() - 1);
        } else if (track.type == "sub") {
            SendMessageW(subtitles_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(label).c_str()));
            subtitleTrackIds_.push_back(track.id);
            if (track.selected) subtitleSelection = static_cast<int>(subtitleTrackIds_.size() - 1);
        } else if (track.type == "video") {
            SendMessageW(videoQuality_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(label).c_str()));
            videoTrackIds_.push_back(track.id);
            if (track.selected) videoSelection = static_cast<int>(videoTrackIds_.size() - 1);
        }
    }
    if (!audioTrackIds_.empty()) SendMessageW(audio_, CB_SETCURSEL, std::max(0, audioSelection), 0);
    SendMessageW(subtitles_, CB_SETCURSEL, subtitleSelection, 0);
    if (!videoTrackIds_.empty()) SendMessageW(videoQuality_, CB_SETCURSEL, std::max(0, videoSelection), 0);
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
    statisticsVisible_ = !statisticsVisible_;
    ShowWindow(stats_, statisticsVisible_ ? SW_SHOWNA : SW_HIDE);
    if (statisticsVisible_) SetWindowPos(stats_, HWND_TOP, 16, 16, 540, 275, SWP_NOACTIVATE);
    UpdateActiveTimer();
}

void PlayerWindow::ApplyShaderHotkey(int hotkey) {
    const auto* preset = shaders_.ForHotkey(hotkey);
    if (!preset) return;
    const auto index = static_cast<std::size_t>(preset - shaders_.Presets().data());
    ApplyShaderPreset(index);
    SendMessageW(shader_, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

void PlayerWindow::ApplyShaderPreset(std::size_t index) {
    if (index == shaders_.Presets().size()) { OpenCustomShaders(); return; }
    if (index >= shaders_.Presets().size()) return;
    try {
        engine_.SetShaders(shaders_.Resolve(shaders_.Presets()[index]));
    } catch (const std::exception& error) {
        ShowError(L"Shader preset", error.what());
        engine_.SetShaders({});
        SendMessageW(shader_, CB_SETCURSEL, 0, 0);
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
    if (!GetOpenFileNameW(&dialog)) { SendMessageW(shader_, CB_SETCURSEL, 0, 0); return; }
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
        SendMessageW(shader_, CB_SETCURSEL, static_cast<WPARAM>(shaders_.Presets().size()), 0);
    } catch (const std::exception& error) {
        ShowError(L"Custom shaders", error.what());
        engine_.SetShaders({});
        SendMessageW(shader_, CB_SETCURSEL, 0, 0);
    }
}

void PlayerWindow::ShowSettingsMenu() {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 2001, L"Cache: Low latency");
    AppendMenuW(menu, MF_STRING, 2002, L"Cache: Balanced");
    AppendMenuW(menu, MF_STRING, 2003, L"Cache: Unstable connection");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2010, L"Hardware decoding: Auto");
    AppendMenuW(menu, MF_STRING, 2011, L"Hardware decoding: Off");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2020, L"Frame pacing: Display resample");
    AppendMenuW(menu, MF_STRING, 2021, L"Frame pacing: Audio clock");
    RECT button{};
    GetWindowRect(settingsButton_, &button);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN,
                                        button.right, button.top, 0, window_, nullptr);
    DestroyMenu(menu);
    try {
        if (command >= 2001 && command <= 2003) {
            const std::string value = command == 2001 ? "low-latency" : command == 2003 ? "unstable" : "balanced";
            engine_.ConfigureCache(value);
            config_.Set("network.cache_mode", value);
        } else if (command == 2010 || command == 2011) {
            const bool enabled = command == 2010;
            engine_.SetHardwareDecoding(enabled);
            config_.Set("playback.hwdec", enabled ? "auto" : "no");
        } else if (command == 2020 || command == 2021) {
            const std::string value = command == 2020 ? "display-resample" : "audio";
            engine_.SetVideoSync(value);
            config_.Set("playback.video_sync", value);
        }
        if (command != 0) config_.Save(paths_.config / "player.conf");
    } catch (const std::exception& error) {
        ShowError(L"Settings", error.what());
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
    if (GetOpenFileNameW(&dialog)) engine_.Open(WideToUtf8(path.c_str()));
}

void PlayerWindow::OpenUrlDialog() {
    if (auto value = PromptForUrl(window_, instance_); value && !value->empty()) ResolveUrl(WideToUtf8(*value));
}

void PlayerWindow::ResolveUrl(std::string value, HeaderMap inheritedHeaders) {
    const auto parsed = Url::Parse(value);
    if (!parsed) {
        if (std::filesystem::exists(std::filesystem::path(Utf8ToWide(value)))) engine_.Open(value);
        else ShowError(L"Open", "The path does not exist or the URL scheme is not allowed");
        return;
    }
    if (parsed->IsDirectMedia()) {
        std::vector<std::pair<std::string, std::string>> headers(inheritedHeaders.begin(), inheritedHeaders.end());
        engine_.Open(parsed->Value(), headers);
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
    struct Choice final { std::wstring label; const StreamVariant* stream; };
    std::vector<Choice> choices;
    for (const auto& season : result.entry.seasons)
        for (const auto& voice : season.voiceTracks)
            for (const auto& episode : voice.episodes)
                for (const auto& stream : episode.streams) {
                    auto label = season.title + " / " + voice.title + " / " + episode.title + " / " + stream.quality;
                    if (stream.protectedStream) label += " (protected)";
                    choices.push_back({Utf8ToWide(label), &stream});
                }
    if (choices.empty()) { ShowError(L"URL resolver", "Metadata was found, but no public playable stream is available"); return; }
    if (choices.size() == 1 && !choices.front().stream->protectedStream) { OpenVariant(*choices.front().stream); return; }
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    for (std::size_t index = 0; index < choices.size() && index < 500; ++index)
        AppendMenuW(menu, MF_STRING | (choices[index].stream->protectedStream ? MF_GRAYED : 0),
                    1000U + static_cast<UINT>(index), choices[index].label.c_str());
    POINT point{};
    GetCursorPos(&point);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, window_, nullptr);
    DestroyMenu(menu);
    if (command >= 1000 && command < 1000 + choices.size()) OpenVariant(*choices[command - 1000].stream);
}

void PlayerWindow::OpenVariant(const StreamVariant& stream) {
    if (stream.protectedStream) { ShowError(L"Provider", "Provider unsupported: protected/DRM stream"); return; }
    if (stream.protocol == "embed") { ResolveUrl(stream.url, stream.headers); return; }
    std::vector<std::pair<std::string, std::string>> headers(stream.headers.begin(), stream.headers.end());
    engine_.Open(stream.url, headers, stream.audioUrl);
}

void PlayerWindow::HandlePlaybackEvent(PlaybackEvent event) {
    if (event.type == PlaybackEventType::TracksChanged) UpdateTracks();
    else if (event.type == PlaybackEventType::FileLoaded) {
        SetWindowTextW(window_, benchmarkMode_ ? L"WannaViewer — benchmark running" : L"WannaViewer — playing");
        if (benchmarkMode_) {
            benchmarkSamples_.clear();
            benchmarkTick_ = 0;
            benchmarkDroppedBaseline_ = -1;
            benchmarkDelayedBaseline_ = -1;
            benchmarkStart_ = std::chrono::steady_clock::now();
            benchmarkRunning_ = true;
            if (benchmarkProfile_ == "hardware-shader") ApplyShaderHotkey(2);
        }
    }
    else if (event.type == PlaybackEventType::EndFile && benchmarkRunning_) FinishBenchmark();
    else if (event.type == PlaybackEventType::Error) ShowError(Utf8ToWide(event.name), event.value);
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
    const auto wide = Utf8ToWide(detail);
    MessageBoxW(window_, wide.c_str(), std::wstring(title).c_str(), MB_OK | MB_ICONERROR);
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

LRESULT PlayerWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        LayoutControls();
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
        RecordInteraction();
        return 0;
    case WM_TIMER:
        if (wParam == kUiTimer) {
            UpdateUi();
            const bool dropdownOpen = SendMessageW(audio_, CB_GETDROPPEDSTATE, 0, 0) != 0 ||
                                      SendMessageW(subtitles_, CB_GETDROPPEDSTATE, 0, 0) != 0 ||
                                      SendMessageW(videoQuality_, CB_GETDROPPEDSTATE, 0, 0) != 0 ||
                                      SendMessageW(shader_, CB_GETDROPPEDSTATE, 0, 0) != 0;
            if (controlsVisible_ && !timelineDragging_ && !dropdownOpen && GetTickCount64() - lastInteraction_ >= 3000)
                ShowControls(false);
        }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == kPlay && HIWORD(wParam) == BN_CLICKED) engine_.TogglePause();
        else if (id == kFullscreen && HIWORD(wParam) == BN_CLICKED) ToggleFullscreen();
        else if (id == kSettings && HIWORD(wParam) == BN_CLICKED) ShowSettingsMenu();
        else if (id == kAudio && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto index = static_cast<std::size_t>(SendMessageW(audio_, CB_GETCURSEL, 0, 0));
            if (index < audioTrackIds_.size()) engine_.SetAudioTrack(audioTrackIds_[index]);
        } else if (id == kSubtitles && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto index = static_cast<std::size_t>(SendMessageW(subtitles_, CB_GETCURSEL, 0, 0));
            if (index < subtitleTrackIds_.size()) engine_.SetSubtitleTrack(subtitleTrackIds_[index]);
        } else if (id == kShader && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto index = static_cast<std::size_t>(SendMessageW(shader_, CB_GETCURSEL, 0, 0));
            ApplyShaderPreset(index);
        } else if (id == kVideoQuality && HIWORD(wParam) == CBN_SELCHANGE) {
            const auto index = static_cast<std::size_t>(SendMessageW(videoQuality_, CB_GETCURSEL, 0, 0));
            if (index < videoTrackIds_.size()) engine_.SetVideoTrack(videoTrackIds_[index]);
        }
        RecordInteraction();
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == timeline_) {
            const auto code = LOWORD(wParam);
            timelineDragging_ = code == TB_THUMBTRACK;
            if (code == TB_ENDTRACK || code == TB_THUMBPOSITION) {
                const auto position = static_cast<int>(SendMessageW(timeline_, TBM_GETPOS, 0, 0));
                engine_.SeekAbsolute(engine_.Duration() * static_cast<double>(position) / 10000.0);
                timelineDragging_ = false;
            }
        } else if (reinterpret_cast<HWND>(lParam) == volume_) {
            engine_.SetVolume(static_cast<double>(SendMessageW(volume_, TBM_GETPOS, 0, 0)));
        }
        RecordInteraction();
        return 0;
    case WM_KEYDOWN: {
        RecordInteraction();
        const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (control && wParam >= '0' && wParam <= '9') ApplyShaderHotkey(static_cast<int>(wParam - '0'));
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
        else engine_.Open(WideToUtf8(path));
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
        SetTextColor(dc, RGB(235, 235, 235));
        SetBkColor(dc, RGB(12, 12, 12));
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    }
    case WM_ERASEBKGND:
        return 1;
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
