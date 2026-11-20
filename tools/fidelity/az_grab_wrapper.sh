#!/bin/bash
# AZAHAR= wrapper for tools/dev/azahar_test.sh: runs the real Azahar and grabs the Xvfb screen
# every $GRAB_EVERY s into $GRAB_DIR/NNNN.png (real seconds since start). Each grab also appends
# "NNNN <last log.txt timestamp>" to $GRAB_DIR/clock.txt, mapping the app's (emulated) log clock
# to grabs. Same process group, so the test's group kill stops the grabber too.
REAL=${REAL_AZAHAR:-$HOME/azahar/squashfs-root/AppRun}
if [ -n "${GRAB_DIR:-}" ] && [ $# -gt 0 ]; then
    mkdir -p "$GRAB_DIR"
    LOG="${XDG_DATA_HOME:-/nonexistent}/azahar-emu/sdmc/3ds/toontown3ds/log.txt"
    # GRAB_FPS=n also records the screen as $GRAB_DIR/run.mkv from the same t0 (frames by time).
    if [ -n "${GRAB_FPS:-}" ]; then
        nice -n 10 ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -framerate "$GRAB_FPS" -video_size 1280x800 \
            -i "$DISPLAY" -c:v libx264 -preset ultrafast -crf 16 "$GRAB_DIR/run.mkv" </dev/null >/dev/null 2>&1 &
    fi
    ( t0=$(date +%s); e=${GRAB_EVERY:-5}
      while sleep "$e"; do t=$(( $(date +%s) - t0 )); n=$(printf %04d $t)
        nice -n 10 ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -video_size 1280x800 -i "$DISPLAY" -frames:v 1 \
            "$GRAB_DIR/$n.png" </dev/null 2>/dev/null
        echo "$n $(tail -n1 "$LOG" 2>/dev/null | sed -n 's/^\[\([0-9]*\)\].*/\1/p')" >> "$GRAB_DIR/clock.txt"
      done ) &
fi
exec "$REAL" "$@"
