#!/usr/bin/env bash
# One-scan CIA: the game plus all baked data in its RomFS (romfs:/data), so FBI's QR install
# is all a player needs (blank SD card is fine).
#
#   DATA=/path/to/sdmc-out/3ds/toontown3ds/data tools/release/build_one_scan_cia.sh [out.cia]
#
# Needs DEVKITPRO, makerom and bannertool on PATH, and a built toontown3ds.elf
# (make; the Makefile's own 3dsx keeps the small dev RomFS).
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=${1:-$PWD/toontown3ds-one-scan.cia}
DATA=${DATA:?set DATA to the baked data dir (tools/pipeline output)}
STAGE=$PWD/build/romfs-cia
RSF=${RSF:-$PWD/tools/release/one_scan.rsf}

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a romfs/. "$STAGE/"
# Hard links: a full copy is ~600 MB; makerom only reads it.
DATA=$(readlink -f "$DATA")
cp -al "$DATA" "$STAGE/data" 2>/dev/null || cp -a "$DATA" "$STAGE/data"
echo "romfs: $(du -sh "$STAGE" | cut -f1) in $(find "$STAGE" -type f | wc -l) files"

# HOME Menu art: META/icon.png (48x48), and META/banner.bnr if present (a prebuilt banner,
# e.g. a 3D CGFX one), else META/banner.png + META/banner.wav (flat banner + jingle).
# banner.wav: first 2.95 s of phase_3/audio/bgm/tt_theme.ogg, fade in 0.03 s, fade out from 2.15 s,
# 22050 Hz stereo (ffmpeg afade + alimiter).
META=${META:-tools/release/meta}
if [ -f "$META/banner.bnr" ]; then cp "$META/banner.bnr" build/banner.bnr
else bannertool makebanner -i "$META/banner.png" -a "${BANNER_WAV:-$META/banner.wav}" -o build/banner.bnr >/dev/null; fi
bannertool makesmdh -s "Toontown 3DS" -l "Unofficial Open Toontown client" -p "toontown-3ds contributors" \
    -i "$META/icon.png" -o build/icon.icn >/dev/null
( cd build && makerom -f cia -o "$OUT" -elf ../toontown3ds.elf -rsf "$RSF" \
    -icon icon.icn -banner banner.bnr -exefslogo -target t \
    -DAPP_ROMFS="$STAGE" )
ls -l "$OUT"
sha256sum "$OUT"
