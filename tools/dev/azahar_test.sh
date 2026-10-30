#!/usr/bin/env bash
# Unattended Azahar run in Old 3DS mode with the scripted autotest.
#
#   [AUTOTEST_FROM=label] [AUTOTEST_UNTIL=label] [AUTOTEST_ONLY=prefix,prefix] [AUTOTEST_EXTRA="line;line"] tools/dev/azahar_test.sh [app.3dsx|installed .app] [seconds] [out-dir]
#
# Temporarily switches Azahar to Old 3DS / 1x / both screens / no update check,
# then always restores the user's config. Leaves log.txt and decoded shot PNGs in out-dir.
set -euo pipefail
cd "$(dirname "$0")/../.."
APP=${1:-$PWD/toontown3ds.3dsx}
SECS=${2:-62}
OUT=${3:-$(mktemp -d)}
if [ "$(uname)" = Darwin ]; then
    AZ=/Applications/Azahar.app/Contents/MacOS/azahar
    AZ_PAT="Azahar.app/Contents/MacOS/azahar"
    CFG="$HOME/Library/Application Support/Azahar/config/qt-config.ini"
    AZ_DATA="$HOME/Library/Application Support/Azahar"
else
    # Linux: the extracted AppImage (azahar.AppImage --appimage-extract), run under Xvfb.
    AZ=${AZAHAR:-$HOME/azahar/squashfs-root/AppRun}
    # The emulator binary only: "squashfs-root/AppRun" also matches xvfb-run's command line, and
    # SIGKILLing xvfb-run leaves its Xvfb server running (51 had piled up on one box).
    AZ_PAT="squashfs-root/AppRun.wrapped"
    BASE_CFG="$HOME/.config/azahar-emu/qt-config.ini"
    BASE_DATA="$HOME/.local/share/azahar-emu"
    if [ ! -f "$BASE_CFG" ]; then # first run: let Azahar write its default config
        timeout -s KILL 15 xvfb-run -a "$AZ" </dev/null >/dev/null 2>&1 || true
    fi
    # Each Linux run owns its config, writable SDMC and log directory. Large immutable
    # NAND/sysdata trees and the baked game data are symlinked, not copied. This lets UI,
    # network and asset workers run concurrently without changing one another's settings,
    # triggers or screenshots.
    RUN_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/toontown3ds-azahar.XXXXXX")
    RUN_CONFIG="$RUN_ROOT/config"
    RUN_DATA="$RUN_ROOT/data"
    CFG="$RUN_CONFIG/azahar-emu/qt-config.ini"
    AZ_DATA="$RUN_DATA/azahar-emu"
    mkdir -p "$(dirname "$CFG")" "$AZ_DATA/log" "$AZ_DATA/sdmc/3ds/toontown3ds"
    cp "$BASE_CFG" "$CFG"
    for entry in cheats nand shaders sysdata; do
        [ ! -e "$BASE_DATA/$entry" ] || ln -s "$BASE_DATA/$entry" "$AZ_DATA/$entry"
    done
    BASE_SD="$BASE_DATA/sdmc/3ds/toontown3ds"
    [ ! -e "$BASE_SD/data" ] || ln -s "$(readlink -f "$BASE_SD/data")" "$AZ_DATA/sdmc/3ds/toontown3ds/data"
    [ ! -f "$BASE_SD/server.txt" ] || cp "$BASE_SD/server.txt" "$AZ_DATA/sdmc/3ds/toontown3ds/server.txt"
fi
SD="$AZ_DATA/sdmc/3ds/toontown3ds"
mkdir -p "$OUT" "$SD"

cp "$CFG" "$OUT/qt-config.ini.backup"
# App Nap throttles Azahar while its window is hidden: emulated time then falls behind real time
# and the server ejects the client for missing heartbeats (345). Disable it for this run only.
BUNDLE=org.azahar-emu.azahar
if [ "$(uname)" = Darwin ]; then
    NAP_BEFORE=$(defaults read "$BUNDLE" NSAppSleepDisabled 2>/dev/null || echo unset)
    defaults write "$BUNDLE" NSAppSleepDisabled -bool YES
fi
restore() {
    if [ -n "${EMU_PID:-}" ]; then
        if [ "$(uname)" = Darwin ]; then kill "$EMU_PID" 2>/dev/null || true
        else kill -TERM -- "-$EMU_PID" 2>/dev/null || true; fi
    fi
    if [ "$(uname)" = Darwin ]; then cp "$OUT/qt-config.ini.backup" "$CFG"; fi
    if [ "$(uname)" = Darwin ]; then
        if [ "$NAP_BEFORE" = unset ]; then defaults delete "$BUNDLE" NSAppSleepDisabled 2>/dev/null || true
        else defaults write "$BUNDLE" NSAppSleepDisabled -bool "$NAP_BEFORE"; fi
    fi
    rm -f "$SD/autotest" "$SD/autotest_fe"
    if [ "$(uname)" != Darwin ]; then rm -rf "$RUN_ROOT"; fi
}
trap restore EXIT INT TERM HUP

SEDI=(sed -i); [ "$(uname)" = Darwin ] && SEDI=(sed -i '')
"${SEDI[@]}" -e 's/^is_new_3ds=true/is_new_3ds=false/' -e 's/^is_new_3ds\\default=true/is_new_3ds\\default=false/' \
    -e 's/^resolution_factor=[0-9]*/resolution_factor=1/' -e 's/^layout_option=[0-9]*/layout_option=0/' \
    -e 's/^check_for_update_on_start=true/check_for_update_on_start=false/' \
    -e 's/^volume=.*/volume=0/' -e 's/^volume\\default=true/volume\\default=false/' "$CFG" # muted (the user's volume comes back with the config restore)
# No sound device (headless Linux): Auto fails to open cubeb, so pick the Null sink outright.
if [ "$(uname)" != Darwin ] && [ ! -e /dev/snd ]; then
    "${SEDI[@]}" -e 's/^output_type=[0-9]*/output_type=1/' -e 's/^output_type\\default=true/output_type\\default=false/' "$CFG"
fi

rm -f "$SD/log.txt" "$SD"/shot_*.bin
: > "$SD/autotest"
if [ -n "${AUTOTEST_FROM:-}" ]; then echo "from $AUTOTEST_FROM" >> "$SD/autotest"; fi
if [ -n "${AUTOTEST_UNTIL:-}" ]; then echo "until $AUTOTEST_UNTIL" >> "$SD/autotest"; fi
if [ -n "${AUTOTEST_ONLY:-}" ]; then echo "only $AUTOTEST_ONLY" >> "$SD/autotest"; fi
# AUTOTEST_EXTRA: more trigger lines, separated by ';' (e.g. "tutorial;shots off").
if [ -n "${AUTOTEST_EXTRA:-}" ]; then printf '%s\n' "$AUTOTEST_EXTRA" | tr ';' '\n' >> "$SD/autotest"; fi
# AUTOTEST_FE=1 runs the front-end script (title, Pick-a-Toon, Make-a-Toon, names) instead; it
# reads the same from/until/only lines from its own trigger file.
if [ -n "${AUTOTEST_FE:-}" ]; then cp "$SD/autotest" "$SD/autotest_fe"; else rm -f "$SD/autotest_fe"; fi
if [ "$(uname)" = Darwin ]; then
    caffeinate -i "$AZ" "$APP" </dev/null >/dev/null 2>&1 & # no idle sleep mid-run
    EMU_PID=$!
else
    setsid env XDG_CONFIG_HOME="$RUN_CONFIG" XDG_DATA_HOME="$RUN_DATA" \
        xvfb-run -a -s "-screen 0 1280x800x24" "$AZ" "$APP" </dev/null >/dev/null 2>&1 &
    EMU_PID=$!
fi
sleep "$SECS" & wait $! # interruptible: a TERM runs restore() at once

cp "$SD/log.txt" "$OUT/log.txt" 2>/dev/null || echo "no log.txt written"
if ls "$SD"/shot_*.bin >/dev/null 2>&1; then
    .venv/bin/python -I tools/dev/decode_shot.py "$SD"/shot_*.bin --out-dir "$OUT" >/dev/null
fi
grep -c -E "PANIC|Unreachable" "$AZ_DATA/log/azahar_log.txt" \
    | sed 's/^/emulator panics: /' || true
echo "results in $OUT"
