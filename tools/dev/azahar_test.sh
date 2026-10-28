#!/usr/bin/env bash
# Unattended Azahar run in Old 3DS mode with the scripted autotest.
#
#   [TT_OFFLINE=1 | TT_PORT=n TT_TOKEN=name] [TT_DATA=dir] (Linux, isolated per run)
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
    # Every run gets its own config, data (sdmc/nand/sysdata) and process group, so parallel agents
    # never share a config, a log or each other's emulator (no global pkill; only this run's group
    # is killed). TT_DATA overrides which baked data set the run sees (default: this tree's).
    AZ=${AZAHAR:-$HOME/azahar/squashfs-root/AppRun}
    PER_RUN=1
    RUN_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/azahar_run.XXXXXX")
    export XDG_DATA_HOME="$RUN_ROOT/data" XDG_CONFIG_HOME="$RUN_ROOT/config"
    CFG="$XDG_CONFIG_HOME/azahar-emu/qt-config.ini"
    AZ_DATA="$XDG_DATA_HOME/azahar-emu"
    mkdir -p "$XDG_CONFIG_HOME/azahar-emu" "$AZ_DATA"
    if [ -f "$HOME/.config/azahar-emu/qt-config.ini" ]; then
        cp "$HOME/.config/azahar-emu/qt-config.ini" "$CFG"
        for d in nand sysdata; do # system archives / keys the user's install already has
            [ -d "$HOME/.local/share/azahar-emu/$d" ] && cp -a "$HOME/.local/share/azahar-emu/$d" "$AZ_DATA/$d"
        done
    else # first run: let Azahar write its default config (into this run's own dirs)
        setsid timeout -s KILL 15 xvfb-run -a "$AZ" </dev/null >/dev/null 2>&1 || true
    fi
fi
SD="$AZ_DATA/sdmc/3ds/toontown3ds"
mkdir -p "$OUT" "$SD"

# macOS: one run at a time (shared user config; a second run would back up the first one's test
# config as "the user's" and its exit would kill the first run's emulator, which looks exactly like
# an app crash). Linux runs are fully isolated (see above) and need no lock.
LOCK="${TMPDIR:-/tmp}/toontown3ds_azahar_test.lock"
if [ -z "${PER_RUN:-}" ]; then
    if ! mkdir "$LOCK" 2>/dev/null; then
        if kill -0 "$(cat "$LOCK/pid" 2>/dev/null)" 2>/dev/null; then
            echo "another azahar_test.sh run (pid $(cat "$LOCK/pid")) is in progress" >&2
            exit 1
        fi
        rm -rf "$LOCK" && mkdir "$LOCK" # a stale lock from a run that was killed
    fi
    echo $$ > "$LOCK/pid"
    pkill -9 -f "$AZ_PAT" 2>/dev/null || true
fi
cp "$CFG" "$OUT/qt-config.ini.backup"
# App Nap throttles Azahar while its window is hidden: emulated time then falls behind real time
# and the server ejects the client for missing heartbeats (345). Disable it for this run only.
BUNDLE=org.azahar-emu.azahar
if [ "$(uname)" = Darwin ]; then
    NAP_BEFORE=$(defaults read "$BUNDLE" NSAppSleepDisabled 2>/dev/null || echo unset)
    defaults write "$BUNDLE" NSAppSleepDisabled -bool YES
fi
EMU_PGID=
restore() {
    if [ -n "${PER_RUN:-}" ]; then
        [ -n "$EMU_PGID" ] && kill -9 -- "-$EMU_PGID" 2>/dev/null || true # this run's group only
        rm -rf "$RUN_ROOT"
        return
    fi
    pkill -9 -f "$AZ_PAT" 2>/dev/null || true
    cp "$OUT/qt-config.ini.backup" "$CFG"
    if [ "$(uname)" = Darwin ]; then
        if [ "$NAP_BEFORE" = unset ]; then defaults delete "$BUNDLE" NSAppSleepDisabled 2>/dev/null || true
        else defaults write "$BUNDLE" NSAppSleepDisabled -bool "$NAP_BEFORE"; fi
    fi
    rm -f "$SD/autotest" "$SD/autotest_fe"
    rm -rf "$LOCK"
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
if [ -n "${PER_RUN:-}" ]; then # the baked data set, read-only through a symlink
    ln -sfn "${TT_DATA:-$PWD/sdmc-out/3ds/toontown3ds/data}" "$SD/data"
    # TT_OFFLINE=1: no network at all (never touches another agent's server). TT_PORT/TT_TOKEN:
    # a private server on its own port and account.
    {   echo "port=${TT_PORT:-7198}"; echo "token=${TT_TOKEN:-perf-$$}"
        [ -n "${TT_OFFLINE:-}" ] && echo "online=0"; } > "$SD/server.txt"
fi
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
    setsid xvfb-run -a -s "-screen 0 1280x800x24" "$AZ" "$APP" </dev/null >/dev/null 2>&1 &
    EMU_PGID=$!
fi
sleep "$SECS" & wait $! # interruptible: a TERM runs restore() at once

cp "$SD/log.txt" "$OUT/log.txt" 2>/dev/null || echo "no log.txt written"
if ls "$SD"/shot_*.bin >/dev/null 2>&1; then
    .venv/bin/python -I tools/dev/decode_shot.py "$SD"/shot_*.bin --out-dir "$OUT" >/dev/null
fi
grep -c -E "PANIC|Unreachable" "$AZ_DATA/log/azahar_log.txt" \
    | sed 's/^/emulator panics: /' || true
echo "results in $OUT"
