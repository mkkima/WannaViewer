# Performance and verification

## Defaults

- Hardware decode: `auto`, with software fallback; copy backends are not forced.
- Renderer: `gpu-next`/libplacebo, D3D11 on Windows.
- Frame pacing: `display-resample`; interpolation is off.
- Network cache: 20 seconds readahead, 150 MiB forward maximum and 32 MiB back
  buffer. The large initial-cache pause is disabled so presentation can start
  as soon as demux/decode are ready; later underrun handling remains enabled.
  Files are never fully loaded into RAM.
- Shader cache: enabled in the portable cache directory.
- Anime4K and automatic shader downgrade: off by default.
- UI and statistics timers: stopped while their overlays are hidden, except for
  the bounded first-frame watchdog while a new source is opening.
- WebView2: created lazily only after Kodik/Alloha is selected and torn down as
  soon as a public media request has been captured.

## Benchmark

```powershell
player.exe --benchmark video.mkv
player.exe --benchmark --benchmark-mode=software video.mkv
player.exe --benchmark --benchmark-mode=hardware-shader video.mkv
```

The result is printed to an attached console when available and written to
`benchmark.json`. It includes codec, size, frame rate, bit depth, HDR transfer,
active decoder, renderer, CPU average/peak, frame drops/delays, and shader chain.
`dropped_frames`/`delayed_frames` exclude the first one-second pipeline warmup;
the corresponding `*_total` fields retain initial decoder/presentation events.
The stable libmpv client API does not expose decode/render/GPU shader timings;
those JSON fields are `null`, not invented. Use GPUView/PresentMon/ETW or vendor
profilers for those measurements.

Run the sample matrix with `scripts/test-media.ps1`. Media is user-supplied or
open/generated and is not committed. Compare `results.json` to a machine-specific
baseline; cross-machine absolute CPU/GPU numbers are not meaningful.

`scripts/ui-smoke-windows.ps1 -Media <file>` exercises playback, pause, seek,
track cycling, fullscreen, statistics, and clean shutdown against the packaged
binary. `scripts/ui-empty-state-smoke-windows.ps1` verifies that every initial
control stays visible and within the window and that the URL dialog opens.
File-dialog and drag-and-drop remain manual interactions.

`scripts/resolver-hls-playback-smoke-windows.ps1` accepts a fresh CVH source
endpoint and passes only when libmpv configures video and advances `time-pos`;
`FileLoaded` alone no longer counts as successful playback.

## Verified on the current Windows host

Generated 1280×720 H.264/AAC 23.976 fps, HEVC Main10/PQ 60 fps, and AV1
Main10/PQ 60 fps samples completed through `d3d11va` and `gpu-next/d3d11` with
zero dropped and zero delayed frames after the one-second warmup. HLS completed
with no dropped frames and 0–1 delayed frames across repeated runs; DASH had no
drops and 2–3 delayed frames. The balanced Anime4K chain had no drops and 0–1
delayed frames on the H.264 sample. These are short functional smoke tests, not evidence for 4K60, correct
HDR display output, every GPU vendor/driver, or long-run leak freedom. Those
require the full media matrix and target displays.
