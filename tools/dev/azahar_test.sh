#!/usr/bin/env bash
# Unattended Azahar run in Old 3DS mode with the scripted autotest.
#
#   [AUTOTEST_FROM=label] [AUTOTEST_UNTIL=label] [AUTOTEST_ONLY=prefix,prefix] [AUTOTEST_EXTRA="line;line"]
#   [SERVER=host:port [TOKEN=account]] tools/dev/azahar_test.sh [app.3dsx|installed .app] [seconds] [out-dir]
#
# Every run starts from a private copy of the app (so rebuilding the tree mid-run is safe) and
# leaves log.txt, the emulator log and decoded shot PNGs in out-dir.
#
# Linux: each run is fully isolated, so several agents can run at once on one box. The run gets
# its own XDG config/data/cache dirs (Azahar's qt-config.ini, nand, sysdata, sdmc and log all live
# there), its own Xvfb display (xvfb-run -a) and kills only its own emulator. The SD data set is
# a symlink to the user's (read-only use); server.txt comes from SERVER, else the user's copy.
# The user's own Azahar config is never touched.
#
# macOS: Azahar has one fixed data dir, so runs still take a lock, switch the user's config to
# test settings and always restore it on exit.
set -euo pipefail
cd "$(dirname "$0")/../.."
APP_SRC=${1:-$PWD/toontown3ds.3dsx}
SECS=${2:-62}
OUT=${3:-$(mktemp -d)}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
RUN=$(mktemp -d "${TMPDIR:-/tmp}/tt3ds_azahar.XXXXXX")
APP="$RUN/app.${APP_SRC##*.}"
cp -R "$APP_SRC" "$APP"

if [ "$(uname)" = Darwin ]; then
    AZ=/Applications/Azahar.app/Contents/MacOS/azahar
    CFG="$HOME/Library/Application Support/Azahar/config/qt-config.ini"
    AZ_DATA="$HOME/Library/Application Support/Azahar"
    USER_SD="$AZ_DATA/sdmc/3ds/toontown3ds"
    SD="$USER_SD"
else
    # Linux: the extracted AppImage (azahar.AppImage --appimage-extract), run under Xvfb.
    AZ=${AZAHAR:-$HOME/azahar/squashfs-root/AppRun}
    USER_CFG="${XDG_CONFIG_HOME:-$HOME/.config}/azahar-emu/qt-config.ini"
    USER_DATA="${XDG_DATA_HOME:-$HOME/.local/share}/azahar-emu"
    USER_SD="$USER_DATA/sdmc/3ds/toontown3ds"
    export XDG_CONFIG_HOME="$RUN/config" XDG_DATA_HOME="$RUN/data" XDG_CACHE_HOME="$RUN/cache"
    CFG="$XDG_CONFIG_HOME/azahar-emu/qt-config.ini"
    AZ_DATA="$XDG_DATA_HOME/azahar-emu"
    SD="$AZ_DATA/sdmc/3ds/toontown3ds"
    mkdir -p "$(dirname "$CFG")" "$SD"
    # Launch the emulator through a per-run symlink: its command line then names only this run's
    # dirs, so another tree's older copy of this script (pkill -f squashfs-root/AppRun.wrapped)
    # cannot kill it. The AppRun hook's library setup is replayed here.
    EMU_DIR=$(dirname "$(readlink -f "$AZ")")
    if [ -x "$EMU_DIR/AppRun.wrapped" ]; then
        ln -s "$EMU_DIR" "$RUN/emu"
        cat > "$RUN/emu-run.sh" <<EOF
#!/usr/bin/env bash
APPDIR="$EMU_DIR"
for h in "$EMU_DIR"/apprun-hooks/*.sh; do [ -f "\$h" ] && source "\$h"; done
exec "$RUN/emu/AppRun.wrapped" "\$@"
EOF
        chmod +x "$RUN/emu-run.sh"
        AZ="$RUN/emu-run.sh"
    fi
    # nand/sysdata are small (system settings, keys); copy so the run cannot change the user's.
    for d in nand sysdata; do
        [ -d "$USER_DATA/$d" ] && cp -R "$USER_DATA/$d" "$AZ_DATA/$d"
    done
    if [ -e "$USER_SD/data" ]; then ln -s "$(readlink -f "$USER_SD/data")" "$SD/data"
    else echo "warning: no SD data set at $USER_SD/data" >&2; fi
    if [ -f "$USER_CFG" ]; then
        cp "$USER_CFG" "$CFG"
    else # no user config: let Azahar write its defaults into this run's dir, then edit those
        timeout -s KILL 15 xvfb-run -a "$AZ" </dev/null >/dev/null 2>&1 || true
    fi
fi
mkdir -p "$SD"

# Kills only this run's emulator. Its command line is the only one holding this run's private
# app path. Never kill xvfb-run itself: it then leaves its Xvfb server running.
kill_emulator() {
    pkill -9 -f "AppRun.wrapped $APP" 2>/dev/null || true
    pkill -9 -f "MacOS/azahar $APP" 2>/dev/null || true
}

if [ "$(uname)" = Darwin ]; then
    # One run at a time: a second run would back up the first one's test config as "the user's".
    LOCK="${TMPDIR:-/tmp}/toontown3ds_azahar_test.lock"
    if ! mkdir "$LOCK" 2>/dev/null; then
        if kill -0 "$(cat "$LOCK/pid" 2>/dev/null)" 2>/dev/null; then
            echo "another azahar_test.sh run (pid $(cat "$LOCK/pid")) is in progress" >&2
            exit 1
        fi
        rm -rf "$LOCK" && mkdir "$LOCK" # a stale lock from a run that was killed
    fi
    echo $$ > "$LOCK/pid"
    # Azahar is single-instance per data dir on macOS.
    pkill -9 -f "Azahar.app/Contents/MacOS/azahar" 2>/dev/null || true
    cp "$CFG" "$OUT/qt-config.ini.backup"
    if [ -f "$SD/server.txt" ]; then cp "$SD/server.txt" "$OUT/server.txt.backup"; fi
    # App Nap throttles Azahar while its window is hidden: emulated time then falls behind real
    # time and the server ejects the client for missing heartbeats (345). Off for this run only.
    BUNDLE=org.azahar-emu.azahar
    NAP_BEFORE=$(defaults read "$BUNDLE" NSAppSleepDisabled 2>/dev/null || echo unset)
    defaults write "$BUNDLE" NSAppSleepDisabled -bool YES
fi

restore() {
    kill_emulator
    if [ "$(uname)" = Darwin ]; then
        cp "$OUT/qt-config.ini.backup" "$CFG"
        if [ -f "$OUT/server.txt.backup" ]; then cp "$OUT/server.txt.backup" "$SD/server.txt"; fi
        if [ "$NAP_BEFORE" = unset ]; then defaults delete "$BUNDLE" NSAppSleepDisabled 2>/dev/null || true
        else defaults write "$BUNDLE" NSAppSleepDisabled -bool "$NAP_BEFORE"; fi
        rm -f "$SD/autotest" "$SD/autotest_fe"
        rm -rf "$LOCK"
    fi
    rm -rf "$RUN"
}
trap restore EXIT INT TERM HUP

if [ -n "${SERVER:-}" ]; then # host:port (+ TOKEN) -> source/core/config.h key=value lines
    printf 'host=%s\nport=%s\n' "${SERVER%:*}" "${SERVER##*:}" > "$SD/server.txt"
    if [ -n "${TOKEN:-}" ]; then echo "token=$TOKEN" >> "$SD/server.txt"; fi
elif [ "$SD" != "$USER_SD" ] && [ -f "$USER_SD/server.txt" ]; then cp "$USER_SD/server.txt" "$SD/server.txt"; fi

SEDI=(sed -i); [ "$(uname)" = Darwin ] && SEDI=(sed -i '')
"${SEDI[@]}" -e 's/^is_new_3ds=true/is_new_3ds=false/' -e 's/^is_new_3ds\\default=true/is_new_3ds\\default=false/' \
    -e 's/^resolution_factor=[0-9]*/resolution_factor=1/' -e 's/^layout_option=[0-9]*/layout_option=0/' \
    -e 's/^check_for_update_on_start=true/check_for_update_on_start=false/' \
    -e 's/^volume=.*/volume=0/' -e 's/^volume\\default=true/volume\\default=false/' "$CFG" # muted
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
    ( caffeinate -i "$AZ" "$APP" </dev/null >/dev/null 2>&1 & ) # no idle sleep mid-run
else
    ( xvfb-run -a -s "-screen 0 1280x800x24" "$AZ" "$APP" </dev/null >/dev/null 2>&1 & )
fi
sleep "$SECS" & wait $! # interruptible: a TERM runs restore() at once

cp "$SD/log.txt" "$OUT/log.txt" 2>/dev/null || echo "no log.txt written"
cp "$AZ_DATA/log/azahar_log.txt" "$OUT/azahar_log.txt" 2>/dev/null || true
if ls "$SD"/shot_*.bin >/dev/null 2>&1; then
    .venv/bin/python -I tools/dev/decode_shot.py "$SD"/shot_*.bin --out-dir "$OUT" >/dev/null
    [ "$SD" = "$USER_SD" ] || rm -f "$SD"/shot_*.bin
fi
grep -c -E "PANIC|Unreachable" "$OUT/azahar_log.txt" 2>/dev/null | sed 's/^/emulator panics: /' || true
echo "results in $OUT"
