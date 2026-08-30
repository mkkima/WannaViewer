# Architecture

WannaViewer keeps decoded video outside application memory. libmpv owns demux,
decode, GPU interop, libplacebo processing, shaders, and color management. The
native shells provide a GPU render target, controls, configuration, and
asynchronous commands; they never request CPU video frames.

```text
Qt Widgets/AppKit UI thread
        │ async commands / copied events
        ▼
MpvEngine event thread ── libmpv ── FFmpeg decode ── GPU surface
                                             │
                                             └── gpu-next/libplacebo
                                                   ├── GLSL chain
                                                   ├── HDR/tone map
                                                   └── OpenGL/Cocoa presentation

URL worker ── ResolverPipeline ── known page adapter ── provider adapter
                  ├── direct media (no HTTP preflight)
                  ├── allowlisted WebView2 embed (Kodik/Alloha, Windows)
                  ├── yt-dlp child process
                  └── bounded static-HTML fallback
```

## Runtime boundaries

- `MpvEngine` owns the libmpv handle. Its event thread blocks in
  `mpv_wait_event(-1)` and is stopped with `mpv_wakeup`; there is no polling
  loop. libmpv client calls are thread-safe and UI commands are asynchronous.
- On Windows, a Qt Widgets shell owns the layout, rounded controls, in-window
  selectors, animations, and accessibility. A `QOpenGLWidget` supplies its GPU
  framebuffer to libmpv's Render API. This avoids a foreign child HWND and its
  composition/flicker problems without copying decoded frames through CPU memory.
- libmpv is prewarmed on a worker so opening the first local file does not block
  the UI thread. Once Qt has created the OpenGL context, the GUI thread attaches
  the Render API. The empty/opening states remain responsive during both phases.
- The UI receives copied events through queued Qt calls. No libmpv event pointer
  crosses the next `mpv_wait_event` call.
- Resolver work runs on one cancellable worker. HTTP is bounded by scheme,
  timeout, redirect, and response-size policies.
- The Windows Kodik/Alloha fallback creates WebView2 on that resolver worker's
  STA apartment, pumps only its local browser work, denies pop-ups/permissions,
  observes public media requests, then closes the controller and removes its
  isolated session profile.
- yt-dlp receives a real argv array, runs without a shell, has bounded output,
  and is placed in a kill-on-close Windows Job Object.
- Statistics and timeline sampling run only while their UI is visible. Benchmark
  sampling is explicitly activated by `--benchmark`.

## Windows render policy

The production Qt player is configured with `vo=libmpv`, `hwdec=auto`, automatic
output format/color space, and `target-colorspace-hint=yes`. libmpv/libplacebo
renders into Qt's OpenGL framebuffer. On supported hardware, `hwdec=auto` keeps
decode surfaces on the GPU and falls back to software decoding if unsupported.

`d3d11va-zero-copy=yes` is deliberately not forced. mpv documents that it can
remove a GPU-to-GPU surface copy but can expose padding artifacts and driver
bugs. Stability has higher priority; users may test it in a future hardware
profile after measurement.

## Failure behavior

Playback, shaders, and resolvers report errors without terminating the UI.
Missing yt-dlp disables only that resolver. A failed hardware decoder falls back
inside mpv. The startup watchdog allows 30 seconds for local media and 60 seconds
for network media; a video is reported as playing only after time advances and a
frame reaches Qt's framebuffer. Remote headers are replaced per load and secrets
are redacted before logging. A browser-provider source rejected before its first
frame is refreshed once; the retry budget prevents loops. DRM/protected sources
remain metadata only and are never bypassed.
