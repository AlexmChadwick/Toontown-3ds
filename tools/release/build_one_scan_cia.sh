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
META=${META:-tools/release/meta}

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a romfs/. "$STAGE/"
# Hard links: a full copy is ~600 MB; makerom only reads it.
DATA=$(readlink -f "$DATA")
cp -al "$DATA" "$STAGE/data" 2>/dev/null || cp -a "$DATA" "$STAGE/data"
echo "romfs: $(du -sh "$STAGE" | cut -f1) in $(find "$STAGE" -type f | wc -l) files"

# 3D HOME Menu banner (CBMD + BCWAV). Regenerate with tools/banner/build.sh (needs ref/resources).
if [ "${SKIP_BANNER_BUILD:-0}" != 1 ]; then
  RESOURCES="${RESOURCES:-$PWD/ref/resources}" OUT_DIR="$PWD/$META" "$PWD/tools/banner/build.sh"
fi
export PATH="$PWD/tools/banner/bin:${PATH:-}"

if [ -f "$META/banner.bnr" ]; then
  cp "$META/banner.bnr" build/banner.bnr
else
  echo "error: missing $META/banner.bnr (run tools/banner/build.sh)" >&2
  exit 1
fi

bannertool makesmdh -s "Toontown 3DS" -l "Unofficial Open Toontown client" -p "toontown-3ds contributors" \
    -f visible,allow3d,recordusage,extendedbanner \
    -i "$META/icon.png" -o build/icon.icn >/dev/null
( cd build && makerom -f cia -o "$OUT" -elf ../toontown3ds.elf -rsf "$RSF" \
    -icon icon.icn -banner banner.bnr -exefslogo -target t \
    -DAPP_ROMFS="$STAGE" )
ls -l "$OUT"
sha256sum "$OUT"
