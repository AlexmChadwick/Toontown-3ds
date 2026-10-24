#!/usr/bin/env bash
# Unattended Azahar run in Old 3DS mode with the scripted autotest.
#
#   [AUTOTEST_FROM=label] [AUTOTEST_UNTIL=label] [AUTOTEST_ONLY=prefix,prefix] tools/dev/azahar_test.sh [app.3dsx|installed .app] [seconds] [out-dir]
#
# Temporarily switches Azahar to Old 3DS / 1x / both screens / no update check,
# then always restores the user's config. Leaves log.txt and decoded shot PNGs in out-dir.
set -euo pipefail
cd "$(dirname "$0")/../.."
APP=${1:-$PWD/toontown3ds.3dsx}
SECS=${2:-62}
OUT=${3:-$(mktemp -d)}
AZ=/Applications/Azahar.app/Contents/MacOS/azahar
CFG="$HOME/Library/Application Support/Azahar/config/qt-config.ini"
SD="$HOME/Library/Application Support/Azahar/sdmc/3ds/toontown3ds"
mkdir -p "$OUT" "$SD"

pkill -9 -f "Azahar.app/Contents/MacOS/azahar" 2>/dev/null || true
cp "$CFG" "$OUT/qt-config.ini.backup"
restore() {
    pkill -9 -f "Azahar.app/Contents/MacOS/azahar" 2>/dev/null || true
    cp "$OUT/qt-config.ini.backup" "$CFG"
    rm -f "$SD/autotest"
}
trap restore EXIT

sed -i '' -e 's/^is_new_3ds=true/is_new_3ds=false/' -e 's/^is_new_3ds\\default=true/is_new_3ds\\default=false/' \
    -e 's/^resolution_factor=[0-9]*/resolution_factor=1/' -e 's/^layout_option=[0-9]*/layout_option=0/' \
    -e 's/^check_for_update_on_start=true/check_for_update_on_start=false/' \
    -e 's/^volume=.*/volume=0/' "$CFG" # muted (the user's volume comes back with the config restore)

rm -f "$SD/log.txt" "$SD"/shot_*.bin
: > "$SD/autotest"
if [ -n "${AUTOTEST_FROM:-}" ]; then echo "from $AUTOTEST_FROM" >> "$SD/autotest"; fi
if [ -n "${AUTOTEST_UNTIL:-}" ]; then echo "until $AUTOTEST_UNTIL" >> "$SD/autotest"; fi
if [ -n "${AUTOTEST_ONLY:-}" ]; then echo "only $AUTOTEST_ONLY" >> "$SD/autotest"; fi
( "$AZ" "$APP" </dev/null >/dev/null 2>&1 & )
sleep "$SECS"

cp "$SD/log.txt" "$OUT/log.txt" 2>/dev/null || echo "no log.txt written"
if ls "$SD"/shot_*.bin >/dev/null 2>&1; then
    .venv/bin/python -I tools/dev/decode_shot.py "$SD"/shot_*.bin --out-dir "$OUT" >/dev/null
fi
grep -c -E "PANIC|Unreachable" "$HOME/Library/Application Support/Azahar/log/azahar_log.txt" \
    | sed 's/^/emulator panics: /' || true
echo "results in $OUT"
