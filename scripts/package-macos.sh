#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
source_app="$project_root/build/macos-arm64-release/bin/WannaViewer.app"
destination_root="$project_root/dist/macos-arm64"
destination_app="$destination_root/WannaViewer.app"

test -d "$source_app"
rm -rf "$destination_root"
mkdir -p "$destination_root"
ditto "$source_app" "$destination_app"
frameworks="$destination_app/Contents/Frameworks"
mkdir -p "$frameworks"

mpv_library="$project_root/.deps/mpv/lib/libmpv.2.dylib"
test -n "$mpv_library"
test -f "$mpv_library"
cp -fL "$mpv_library" "$frameworks/libmpv.2.dylib"

declare -a queue=("$destination_app/Contents/MacOS/WannaViewer" "$frameworks/libmpv.2.dylib")
declare -A visited=()
while ((${#queue[@]})); do
  binary="${queue[0]}"
  queue=("${queue[@]:1}")
  [[ -n "${visited[$binary]:-}" ]] && continue
  visited[$binary]=1
  while IFS= read -r dependency; do
    [[ "$dependency" == /System/* || "$dependency" == /usr/lib/* || "$dependency" == @* ]] && continue
    [[ -f "$dependency" ]] || continue
    name="$(basename "$dependency")"
    target="$frameworks/$name"
    if [[ ! -f "$target" ]]; then cp -fL "$dependency" "$target"; chmod u+w "$target"; fi
    install_name_tool -change "$dependency" "@rpath/$name" "$binary"
    queue+=("$target")
  done < <(otool -L "$binary" | tail -n +2 | awk '{print $1}')
  if [[ "$binary" == "$frameworks"/* ]]; then install_name_tool -id "@rpath/$(basename "$binary")" "$binary"; fi
done
install_name_tool -add_rpath '@executable_path/../Frameworks' "$destination_app/Contents/MacOS/WannaViewer" 2>/dev/null || true
codesign --force --deep --sign - "$destination_app"
echo "Portable app: $destination_app"
