# Architecture

WannaViewer keeps decoded video outside application memory. libmpv owns demux,
decode, GPU interop, libplacebo processing, shaders, color management, and
presentation. The native shells provide a window handle, controls, configuration,
and asynchronous commands; they never request CPU video frames.

```text
Win32/AppKit UI thread
        │ async commands / copied events
        ▼
MpvEngine event thread ── libmpv ── FFmpeg decode ── GPU surface
                                             │
                                             └── gpu-next/libplacebo
                                                   ├── GLSL chain
                                                   ├── HDR/tone map
                                                   └── D3D11/Cocoa presentation

URL worker ── ResolverPipeline ── known page adapter ── provider adapter
                  ├── direct media (no HTTP preflight)
                  ├── yt-dlp child process
                  └── bounded static-HTML fallback
```

## Runtime boundaries

- `MpvEngine` owns the libmpv handle. Its event thread blocks in
  `mpv_wait_event(-1)` and is stopped with `mpv_wakeup`; there is no polling
  loop. libmpv client calls are thread-safe and UI commands are asynchronous.
- The UI receives copied events through native message queues. No libmpv event
  pointer crosses the next `mpv_wait_event` call.
- Resolver work runs on one cancellable worker. HTTP is bounded by scheme,
  timeout, redirect, and response-size policies.
- yt-dlp receives a real argv array, runs without a shell, has bounded output,
  and is placed in a kill-on-close Windows Job Object.
- Statistics and timeline sampling run only while their UI is visible. Benchmark
  sampling is explicitly activated by `--benchmark`.

## Windows render policy

The pinned mpv build is configured with `vo=gpu-next`, `gpu-api=d3d11`,
`gpu-context=d3d11`, `hwdec=auto`, automatic output format/color space, and
`target-colorspace-hint=yes`. On supported hardware, `hwdec=auto` selects direct
D3D11VA surfaces and falls back to software decoding if unsupported.

`d3d11va-zero-copy=yes` is deliberately not forced. mpv documents that it can
remove a GPU-to-GPU surface copy but can expose padding artifacts and driver
bugs. Stability has higher priority; users may test it in a future hardware
profile after measurement.

## Failure behavior

Playback, shaders, and resolvers report errors without terminating the UI.
Missing yt-dlp disables only that resolver. A failed hardware decoder falls back
inside mpv. Remote headers are replaced per load and secrets are redacted before
logging. DRM/protected sources remain metadata only and are never bypassed.
