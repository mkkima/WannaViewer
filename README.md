# WannaViewer

WannaViewer is a small native frontend for libmpv. Its default playback path is
hardware decoding into GPU surfaces, `vo=gpu-next`/libplacebo rendering, and a
native D3D11 presentation path on Windows. It does not copy decoded frames into
the application.

## Build and run on Windows 11

Requirements: Visual Studio 2022 or newer with the C++ workload, CMake 3.28+,
PowerShell 7, and an internet connection for the first dependency bootstrap.

```powershell
./scripts/build-windows.ps1 -Release
./dist/windows-x64/player.exe
```

The build script downloads a pinned libmpv development archive, checks its
SHA-256 digest, builds, tests, and creates the portable directory. No codec pack
or system mpv installation is used.

Portable configuration/cache/logs are the default. Create the empty marker
`config/use-user-config` before launch to redirect writable configuration,
cache, and logs to the normal per-user application-data directory; packaged
shaders/resolver manifests and tools remain beside the executable.

Drop a media file onto the window, use **Ctrl+O**, or pass a file/direct URL on
the command line. Use **Ctrl+U** for a URL. Controls disappear while playing.

Important keys: Space play/pause, arrows seek, F fullscreen, M mute, S subtitle,
A audio track, F10 statistics, Ctrl+0…4 shader presets, Esc leave fullscreen.

Site resolution is best-effort and limited to publicly accessible metadata and
streams. Protected/DRM media, authentication bypasses, CAPTCHA bypasses, and
paywall circumvention are intentionally unsupported.

See [BUILDING.md](BUILDING.md), [ARCHITECTURE.md](ARCHITECTURE.md), and
[DEPENDENCIES.md](DEPENDENCIES.md) for details and verified limitations.
