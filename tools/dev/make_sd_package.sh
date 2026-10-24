#!/usr/bin/env bash
# Assemble the SD-card install tree for real hardware (or Azahar's SD folder).
#
# Output (default dist/sd/, merge it into the root of the SD card):
#   3ds/toontown3ds/toontown3ds.3dsx        Homebrew Launcher app
#   3ds/toontown3ds/data/...                baked SD data set (zones, interiors, toon, audio, chars, ...)
#   3ds/toontown3ds/server.txt              placeholder server config (created once, never overwritten)
#
# The .cia is not part of this tree: install it with FBI (see README.md "Install on a 3DS").
#
# Usage:
#   tools/dev/make_sd_package.sh [--src DIR] [--3dsx FILE] [--out DIR]
#     --src   baked data root      (default: sdmc-out/3ds/toontown3ds/data)
#     --3dsx  built homebrew app   (default: toontown3ds.3dsx; run `make` first)
#     --out   output root          (default: dist/sd)
#
# Only rsync and coreutils are used. Nothing is downloaded.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

SRC="$ROOT/sdmc-out/3ds/toontown3ds/data"
APP="$ROOT/toontown3ds.3dsx"
OUT="$ROOT/dist/sd"

while [ $# -gt 0 ]; do
    case "$1" in
        --src)  SRC="$2"; shift 2 ;;
        --3dsx) APP="$2"; shift 2 ;;
        --out)  OUT="$2"; shift 2 ;;
        -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
        *) echo "error: unknown argument '$1' (see --help)" >&2; exit 2 ;;
    esac
done

command -v rsync >/dev/null || { echo "error: rsync not found on PATH" >&2; exit 1; }

if [ ! -f "$APP" ]; then
    echo "error: $APP not found. Run 'make' first (or pass --3dsx FILE)." >&2
    exit 1
fi

if [ ! -d "$SRC" ]; then
    echo "error: SD data set not found at $SRC" >&2
    echo "       Bake it first (commands: README.md, section 'Bake the SD data set'), or pass --src DIR." >&2
    exit 1
fi

APPDIR="$OUT/3ds/toontown3ds"
mkdir -p "$APPDIR/data"

# 3DSX: plain copy, overwritten on every run.
cp "$APP" "$APPDIR/toontown3ds.3dsx"

# Data set: mirror the bake output. --delete removes stale bakes from a previous run.
# Desktop junk is excluded so it never reaches the card.
rsync -a --delete \
    --exclude '.DS_Store' --exclude '._*' --exclude 'Thumbs.db' --exclude '*.tmp' \
    "$SRC/" "$APPDIR/data/"

# server.txt: written only if missing, so a hand-edited file survives repackaging.
if [ ! -e "$APPDIR/server.txt" ]; then
    cat > "$APPDIR/server.txt" <<'EOF'
# Toontown 3DS server settings. Read at startup from sdmc:/3ds/toontown3ds/server.txt.
# One key=value per line; '#' starts a comment. Unknown keys and a missing file are fine
# (defaults apply: host 127.0.0.1, port 7198, token 3ds-player, online 1).
#
# Replace host with the LAN IP of the computer running tools/server/start.sh.
# On a 3DS, 127.0.0.1 means the console itself, so it will not reach your PC.
host=192.168.1.20
port=7198
# Any token works on a dev server; each new token creates an account and a toon.
token=3ds-player
# 1 = connect to the server above; 0 = offline (no network at all).
online=1
EOF
    echo "created $APPDIR/server.txt (placeholder: edit host=)"
else
    echo "kept existing $APPDIR/server.txt"
fi

echo "package: $OUT"
echo "  app:  $(du -h "$APPDIR/toontown3ds.3dsx" | cut -f1)  $APPDIR/toontown3ds.3dsx"
echo "  data: $(du -sh "$APPDIR/data" | cut -f1)  $APPDIR/data"
for d in "$APPDIR/data"/*/; do
    [ -d "$d" ] && printf '    %-12s %s\n' "$(basename "$d")" "$(du -sh "$d" | cut -f1)"
done
echo "  total: $(du -sh "$OUT" | cut -f1)"
echo "copy the contents of $OUT to the root of the SD card (merge the 3ds folder)."
