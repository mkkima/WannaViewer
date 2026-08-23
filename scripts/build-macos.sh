#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
mpv_prefix="${WANNAVIEWER_MPV_ROOT:-$(brew --prefix mpv)}"
dependency_root="$project_root/.deps/mpv"

mkdir -p "$dependency_root/include" "$dependency_root/lib"
ditto "$mpv_prefix/include" "$dependency_root/include"
for library in "$mpv_prefix"/lib/libmpv*.dylib; do
  cp -f "$library" "$dependency_root/lib/"
done

cmake --preset macos-arm64-release -DWANNAVIEWER_MPV_ROOT="$dependency_root"
cmake --build --preset macos-arm64-release
ctest --preset macos-arm64-release
"$project_root/scripts/package-macos.sh"
