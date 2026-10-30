#!/usr/bin/env bash
# Build a stereoscopic HOME Menu banner (.bnr) for CIA packaging.
#
# Outputs (default OUT_DIR=tools/release/meta):
#   banner.bnr       CBMD + BCWAV for makerom/bannertool
#   banner.cgfx      3D model (from game assets via glTF → pycgfx)
#   banner.bcwav     ~3 s tt_theme jingle
#   banner_preview.png  flat render for docs / PR
#
# Requires:
#   - ref/resources submodule (phase_3 maps/audio, phase_3/4 models)
#   - Python 3 with panda3d==1.10.15, pillow, numpy (same as tools/pipeline)
#   - ffmpeg
#   - g++ to build bannertool once (tools/banner/bin/bannertool)
#   - skyfloogle/pycgfx cloned into tools/banner/third_party/pycgfx (auto on first run)
#
#   RESOURCES=/path/to/resources OUT_DIR=meta tools/banner/build.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

RESOURCES="${RESOURCES:-$ROOT/ref/resources}"
OUT_DIR="${OUT_DIR:-$ROOT/tools/release/meta}"
BUILD="$ROOT/build/banner"
PYCGFX_DIR="${PYCGFX_DIR:-$ROOT/tools/banner/third_party/pycgfx}"
BANNERTOOL="${BANNERTOOL:-$ROOT/tools/banner/bin/bannertool}"

if [ ! -d "$RESOURCES/phase_3" ]; then
  echo "error: missing $RESOURCES (run: git submodule update --init ref/resources)" >&2
  exit 1
fi

mkdir -p "$BUILD" "$(dirname "$BANNERTOOL")"

if [ ! -x "$BANNERTOOL" ]; then
  echo "building bannertool..."
  BT_SRC="${BANNERTOOL_SRC:-/tmp/3ds-bannertool}"
  if [ ! -d "$BT_SRC/source" ]; then
    git clone --depth 1 https://github.com/carstene1ns/3ds-bannertool.git "$BT_SRC"
  fi
  sed -i 's/u8 pad\[padLength\] = {0};/u8 pad[4] = {0};/' "$BT_SRC/source/3ds/lz11.cpp"
  g++ -O2 -std=c++20 -DVERSION='"1.2.3"' -I"$BT_SRC/source" -I"$BT_SRC/source/pc" \
    -o "$BANNERTOOL" $(find "$BT_SRC/source" -name '*.cpp')
fi

if [ ! -f "$PYCGFX_DIR/main.py" ]; then
  echo "cloning pycgfx..."
  mkdir -p "$(dirname "$PYCGFX_DIR")"
  git clone --depth 1 https://github.com/skyfloogle/pycgfx.git "$PYCGFX_DIR"
fi

PYTHON="${PYTHON:-python3}"
if ! "$PYTHON" -c "import panda3d.core" 2>/dev/null; then
  echo "error: panda3d not installed for $PYTHON (pip install 'panda3d==1.10.15' pillow numpy)" >&2
  exit 1
fi

"$PYTHON" "$ROOT/tools/banner/make_banner_scene.py" --resources "$RESOURCES" --out-dir "$BUILD"

THEME="$RESOURCES/phase_3/audio/bgm/tt_theme.ogg"
WAV="$BUILD/banner_jingle.wav"
ffmpeg -y -nostdin -loglevel error -i "$THEME" -t 2.95 -af "afade=t=in:st=0:d=0.03,afade=t=out:st=2.15:d=0.8,alimiter=limit=0.95" \
  -ar 22050 -ac 2 "$WAV"

"$PYTHON" "$PYCGFX_DIR/main.py" "$BUILD/banner_scene.glb" "$BUILD/banner.cgfx"
CGFX_SIZE=$(stat -c%s "$BUILD/banner.cgfx")
if [ "$CGFX_SIZE" -gt 524288 ]; then
  echo "error: banner.cgfx is ${CGFX_SIZE} bytes (>512 KiB HOME Menu limit)" >&2
  exit 1
fi

"$BANNERTOOL" makecwav -i "$WAV" -o "$BUILD/banner.bcwav" >/dev/null
"$BANNERTOOL" makebanner -ci "$BUILD/banner.cgfx" -ca "$BUILD/banner.bcwav" -o "$BUILD/banner.bnr" >/dev/null

mkdir -p "$OUT_DIR"
cp "$BUILD/banner.bnr" "$BUILD/banner.cgfx" "$BUILD/banner.bcwav" "$OUT_DIR/"
if [ ! -f "$BUILD/banner_preview.png" ]; then
  "$PYTHON" "$ROOT/tools/banner/make_preview_flat.py" "$RESOURCES" "$BUILD/banner_preview.png"
fi
cp "$BUILD/banner_preview.png" "$OUT_DIR/"

# Makefile still reads meta/ for dev .cia builds.
if [ "$OUT_DIR" != "$ROOT/meta" ]; then
  cp "$BUILD/banner.bnr" "$BUILD/banner.cgfx" "$BUILD/banner.bcwav" "$BUILD/banner_preview.png" "$ROOT/meta/"
fi

echo "banner: $OUT_DIR/banner.bnr ($(stat -c%s "$OUT_DIR/banner.bnr") bytes, cgfx ${CGFX_SIZE} bytes)"
