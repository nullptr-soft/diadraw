#!/bin/sh
# Generates the native apps' icons from diadraw.svg (the same artwork as apps/desktop/icon.svg).
# Needs rsvg-convert and ImageMagick; iconutil (macOS) for the .icns. The outputs are committed,
# so building the apps does not need these tools.
set -eu
cd "$(dirname "$0")"
png() { rsvg-convert -w "$1" -h "$1" diadraw.svg -o "$2"; }

# macOS: an .icns from an iconset.
if command -v iconutil >/dev/null; then
  set_dir=$(mktemp -d)/DiaDraw.iconset
  mkdir -p "$set_dir"
  for s in 16 32 128 256 512; do
    png $s "$set_dir/icon_${s}x${s}.png"
    png $((s * 2)) "$set_dir/icon_${s}x${s}@2x.png"
  done
  iconutil -c icns "$set_dir" -o DiaDraw.icns
fi

# Linux: the hicolor theme sizes, plus the SVG as the scalable one.
mkdir -p hicolor
for s in 16 24 32 48 64 128 256 512; do png $s "hicolor/${s}.png"; done

# Windows: a multi-size .ico, and the MSIX tile and store logos.
tmp=$(mktemp -d)
for s in 16 24 32 48 64 256; do png $s "$tmp/$s.png"; done
magick "$tmp/16.png" "$tmp/24.png" "$tmp/32.png" "$tmp/48.png" "$tmp/64.png" "$tmp/256.png" DiaDraw.ico
mkdir -p msix
png 44 msix/Square44x44Logo.png
png 150 msix/Square150x150Logo.png
png 50 msix/StoreLogo.png
rm -rf "$tmp"
