# Building

## Windows 11 x64

Install Visual Studio with the Desktop development with C++ workload, CMake
3.28+, Ninja, Git, and PowerShell 7. Then run:

```powershell
./scripts/build-windows.ps1 -Release
```

The script performs these fail-fast steps:

1. downloads pinned libmpv, Anime4K, and yt-dlp artifacts;
2. verifies every SHA-256 digest;
3. configures and builds with C++23, `/W4 /WX`, Control Flow Guard-compatible
   linker settings, DEP, ASLR, and release LTO when supported;
4. runs CTest;
5. creates `dist/windows-x64`.

Use `-DebugBuild` for a debug build and `-SkipYtDlp` to verify that local playback
remains functional without the helper. The package needs no registry writes,
administrator access, codec packs, system FFmpeg, or system mpv.

## macOS Apple Silicon

Install Xcode command-line tools, CMake, Ninja, and mpv with Homebrew, then run:

```bash
./scripts/build-macos.sh
```

The packaging script recursively copies non-system dylib dependencies into the
app, rewrites load paths to `@rpath`, and applies an ad-hoc signature. Public
distribution still requires a real Developer ID signature and notarization.
The macOS build cannot be validated from the Windows CI/development host.

## Manual presets

```powershell
cmake --preset windows-x64-debug
cmake --build --preset windows-x64-debug
ctest --preset windows-x64-debug
```

Do not use an unverified `libmpv-2.dll`; the bootstrap pin is part of the tested
ABI and licensing baseline.
