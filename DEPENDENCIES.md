# Dependency lock and licensing

| Component | Pin | Artifact / integrity | Purpose | License impact |
|---|---|---|---|---|
| mpv Windows dev build | release `20260814`, mpv git `7b8915bc1d` | `mpv-dev-x86_64-20260814-git-7b8915bc1d.7z`, SHA-256 `0af22b28e920620036d3ae08fd9283156dc9af0420bf4df84b0e02282094599c` | libmpv, FFmpeg, libplacebo runtime | Prebuilt GPL-enabled distribution; the application is GPL-3.0 |
| nlohmann/json | v3.12.0 | `json.tar.xz`, SHA-256 `42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa` | Machine JSON | MIT |
| Anime4K GLSL | v4.0.1, tag commit `4029bf701ecaa15f163cdc49cffe5501c1acf410` | `Anime4K_v4.0.zip`, SHA-256 `139cd282086457c5adc79caf7b75b8b825091d71c9b54958c18745fea62d7ed7` | Optional shaders | MIT |
| yt-dlp | 2026.08.19 | `yt-dlp.exe`, SHA-256 `66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a` | Optional page extraction | Unlicense |
| Microsoft Edge WebView2 SDK/loader | 1.0.4129.50 | NuGet `microsoft.web.webview2.1.0.4129.50.nupkg`, SHA-256 `d3934f482d484b89fb4825df720c710664e1143a1e90f7b3a60794ef33f473d2` | Sandboxed Kodik/Alloha public embed execution on Windows | Microsoft BSD-style license; bundled license and notice included |

The current mpv DLL reports libplacebo v7.351.0 and an FFmpeg development
snapshot. It is kept outside the source tree and downloaded reproducibly by
digest. It is not interchangeable with the locally installed `mpv.exe`.

`WebView2Loader.dll` is included in the portable package. The Microsoft Edge
WebView2 Evergreen Runtime remains an operating-system runtime dependency for
Kodik/Alloha only; the application reports its absence instead of attempting an
unprompted runtime install. Local files and other resolvers do not depend on it.

The app is licensed under GPL-3.0 because the selected convenient Windows media
binary is GPL-enabled. Do not relabel it as proprietary/LGPL. Direct license
texts and notices are under `THIRD_PARTY_LICENSES`; the project license is
`LICENSE`.

Before redistributing binaries publicly, archive the exact corresponding source
for the pinned mpv-winbuild release and its dependency configuration alongside
the release, verify every enabled FFmpeg/library license, include all notices,
and document any source-offer mechanism required by the chosen GPL distribution
method. This repository does not hide that legal release gate.
