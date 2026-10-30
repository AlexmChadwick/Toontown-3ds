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

RENDER_ENV='LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe'
run_gl() {
  if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a env $RENDER_ENV "$@"
  else
    env $RENDER_ENV "$@"
  fi
}

TOON_PORTRAIT="$BUILD/toon_portrait.npy"
if [ ! -f "$TOON_PORTRAIT" ]; then
  run_gl "$PYTHON" "$ROOT/tools/banner/bake_toon_portrait.py" --resources "$RESOURCES" --out "$TOON_PORTRAIT"
fi

"$PYTHON" "$ROOT/tools/banner/make_banner_scene.py" --resources "$RESOURCES" --out-dir "$BUILD" --toon-npy "$TOON_PORTRAIT"

run_gl "$PYTHON" "$ROOT/tools/banner/render_preview.py" --resources "$RESOURCES" --out-dir "$BUILD" --toon-npy "$TOON_PORTRAIT"

THEME="$RESOURCES/phase_3/audio/bgm/tt_theme.ogg"
WAV="$BUILD/banner_jingle.wav"
# 32000 Hz, 16-bit stereo, at most 2.9 s. bannertool does not resample.
# atrim drops samples at 2.9 s; the fade finishes on that cut so it does not click.
ffmpeg -y -nostdin -loglevel error -i "$THEME" \
  -af "atrim=end=2.9,afade=t=in:st=0:d=0.03,afade=t=out:st=2.62:d=0.28,alimiter=limit=0.95" \
  -ar 32000 -ac 2 -sample_fmt s16 -c:a pcm_s16le "$WAV"
"$PYTHON" - "$WAV" <<'PY'
import array, sys, wave
path = sys.argv[1]
wav = wave.open(path)
channels, rate, width = wav.getnchannels(), wav.getframerate(), wav.getsampwidth()
frames = wav.getnframes()
duration = frames / rate
if (channels, rate, width) != (2, 32000, 2) or duration > 2.9 + 1e-4:
    raise SystemExit(
        f"bad banner wav: {channels} ch, {rate} Hz, {width * 8}-bit, {duration:.4f}s"
    )
samples = array.array("h")
samples.frombytes(wav.readframes(frames))
peak = max(abs(sample) for sample in samples)
if peak >= 32767:
    raise SystemExit(f"banner wav clips (peak {peak})")
print(f"audio: {channels} ch, {rate} Hz, {width * 8}-bit PCM, {duration:.4f}s, peak {peak}")
PY

"$PYTHON" "$ROOT/tools/banner/cgfx_convert.py" "$BUILD/banner_scene.glb" "$BUILD/banner.cgfx"
CGFX_SIZE=$(stat -c%s "$BUILD/banner.cgfx")
if [ "$CGFX_SIZE" -gt 524288 ]; then
  echo "error: banner.cgfx is ${CGFX_SIZE} bytes (>512 KiB HOME Menu limit)" >&2
  exit 1
fi

"$BANNERTOOL" makecwav -i "$WAV" -o "$BUILD/banner.bcwav" >/dev/null
"$BANNERTOOL" makebanner -ci "$BUILD/banner.cgfx" -ca "$BUILD/banner.bcwav" -o "$BUILD/banner.bnr" >/dev/null

mkdir -p "$OUT_DIR"
cp "$BUILD/banner.bnr" "$BUILD/banner.cgfx" "$BUILD/banner.bcwav" "$OUT_DIR/"
cp "$BUILD/banner_preview.png" "$BUILD/banner_preview_stereo.png" "$BUILD/banner_preview_anaglyph.png" "$OUT_DIR/"
# 256×128 letterbox of the 5:3 preview. Makefile uses it only if banner.bnr is missing.
cp "$BUILD/banner.png" "$OUT_DIR/"

# Makefile still reads meta/ for dev .cia builds.
if [ "$OUT_DIR" != "$ROOT/meta" ]; then
  cp "$BUILD/banner.bnr" "$BUILD/banner.cgfx" "$BUILD/banner.bcwav" \
    "$BUILD/banner_preview.png" "$BUILD/banner_preview_stereo.png" "$BUILD/banner_preview_anaglyph.png" \
    "$BUILD/banner.png" \
    "$ROOT/meta/"
fi

echo "banner: $OUT_DIR/banner.bnr ($(stat -c%s "$OUT_DIR/banner.bnr") bytes, cgfx ${CGFX_SIZE} bytes)"
