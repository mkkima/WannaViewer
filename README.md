# WannaViewer

WannaViewer is a small Qt Widgets frontend for libmpv. Its default playback path is
hardware decoding into GPU surfaces and libplacebo rendering through libmpv's
OpenGL Render API into a `QOpenGLWidget` on Windows. Video remains on the GPU;
the application does not copy decoded frames through CPU memory.

## Build and run on Windows 11

Requirements: Visual Studio 2022 or newer with the C++ workload, CMake 3.28+,
PowerShell 7, and an internet connection for the first dependency bootstrap.

```powershell
./scripts/build-windows.ps1 -Release
./dist/windows-x64/player.exe
```

The build script downloads pinned libmpv and Qt 6 archives, checks their SHA-256
digests, builds, tests, and creates the portable directory with the required Qt
DLLs. No codec pack or system mpv installation is used. Kodik/Alloha URL resolution additionally
uses the Microsoft Edge WebView2 Evergreen Runtime normally present on Windows
11; the portable package includes the pinned WebView2 loader, not a fixed-version
browser runtime.

Portable configuration/cache/logs are the default. Create the empty marker
`config/use-user-config` before launch to redirect writable configuration,
cache, and logs to the normal per-user application-data directory; packaged
shaders/resolver manifests and tools remain beside the executable.
Set `ui.animations=false` in `config/player.conf` to disable interface motion;
the Windows shell also honors the operating-system client-animation setting.

Drop a media file onto the window, use **Ctrl+O**, or pass a file/direct URL on
the command line. Use **Ctrl+U** for a URL. Controls disappear while playing.

Important keys: Space play/pause, arrows seek, F fullscreen, M mute, S subtitle,
A audio track, F10 statistics, Ctrl+0…4 shader presets, Esc leave fullscreen.

Site resolution is best-effort and limited to publicly accessible metadata and
streams. Protected/DRM media, authentication bypasses, CAPTCHA bypasses, and
paywall circumvention are intentionally unsupported.

Across the supplied YummyAnime/AnimeGo pages the menu exposes the supported CVH,
AniBoom, Kodik, and Alloha choices each page publishes. Kodik/Alloha are resolved by running their
own public embed in an isolated WebView2 session; no closed provider protocol is
decoded by the application.

See [BUILDING.md](BUILDING.md), [ARCHITECTURE.md](ARCHITECTURE.md), and
[DEPENDENCIES.md](DEPENDENCIES.md) for details and verified limitations.
