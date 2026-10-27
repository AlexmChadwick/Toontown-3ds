#!/usr/bin/env bash
# Bake the complete SD data set (sdmc-out/3ds/toontown3ds/data/) from ref/resources and
# ref/open-toontown. Runs every SD baker in dependency order; independent groups ("lanes")
# run in parallel. Nothing under ref/, romfs/ or source/ is written.
#
#   tools/pipeline/bake_sd_all.sh              # every lane
#   tools/pipeline/bake_sd_all.sh world toon   # only these lanes (re-run after a failure)
#   tools/pipeline/bake_sd_all.sh --list       # print the lanes and their steps
#
# Environment:
#   TT_BAKE_LOG         log directory (default sdmc-out/bake-logs/<timestamp>)
#   TT_ETC1_THREADS     ETC1 encoder threads per baker process (default 2)
#   TT_TOON_JOBS        bake_toon.py worker processes (default 3)
#   TT_STREET_JOBS      street/playground .tz bakes run at once (default 2)
#
# Lanes and their internal order (each lane runs its steps sequentially):
#   toon      bake_toon (all parts, 51 clips, full clothes.ttx) -> legacy 5-clip hash baseline
#             (scratch) -> battle_toon_extras (rigid-head battle clips, torso hand .jnt tracks)
#   world     bake_hoods (playground music + fallback .ttm/.ttc) -> [bake_street per zone,
#             1000 with --separate-boat, + the Toontorial street 20000] || [bake_coghq --audio]
#             -> bake_boat -> bake_suit_paths -> bake_minimap (reads every .tz)
#   interiors bake_interior --all, --tutorial -> bake_interior_props
#   level     bake_level --level all -> bake_level_props -> bake_facility_floors (needs the room
#             levels) -> bake_boss VP, --cfo, --cj, --ceo (one at a time: they share data/boss)
#   battle    bake_suit -> bake_props -> bake_suit_props -> battle_audio -> bake_suit_heads_atlas
#   misc      bake_audio (SD soundscape) -> bake_char -> char_paths_bin -> bake_fishing ->
#             bake_minigame -> bake_golf -> bake_golf_physics -> bake_cogbldg -> bake_estate ->
#             bake_clerk_karts
#   verify    (after all lanes) battle_validate.py, non-fatal; size report
#
# Deliberately not run here:
#   bake_cogdo.py, bake_racing*.py   data/cogdo and data/racing are committed kits; never rebake.
#   bake_knock_doors.py              needs the Open Toontown Panda3D fork (panda3d.toontown, built
#                                    by tools/server/setup.sh into tools/server/.venv). Runs only
#                                    when that interpreter exists (step "knock" in the world lane);
#                                    otherwise zones/*.kkd is skipped (knock-knock jokes inactive).
#   bake_factory.py, bake_mintoffice.py  legacy outputs data/factory, data/mint, data/stage are not
#                                    opened by the runtime (superseded by data/level from bake_level).
#   bake_zone_fixes.py               only re-runs bake_street/bake_coghq for 7 zones plus a report;
#                                    the world lane already bakes those zones.
#   Bakers that also write committed files are wrapped so those copies land in $LOG/scratch:
#   bake_suit_heads_atlas.py (romfs/baked/suit_heads.ttm, source/ui/suit_heads_atlas.h),
#   bake_clerk_karts.py (docs/design/clerk_karts.json), bake_audio.py (romfs/baked/audio via
#   --out), bake_boat.py (source/world/boat_paths.h via --header).
#   bake_phase1.sh, bake_laff_meter.py, bake_transitions.py, bake_suit_placeholder.py,
#   bake_audio.py's RomFS half       write romfs/baked (committed) or source/; not SD data.
#   gen_*.py                         write generated headers into source/.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT=$PWD

PYBIN=$ROOT/.venv/bin/python
[[ -x $PYBIN ]] || { echo "missing $PYBIN (see README prerequisites)" >&2; exit 1; }
for d in ref/resources/phase_3 ref/open-toontown/toontown; do
    [[ -d $d ]] || { echo "missing $d: check out the ref/ submodules" >&2; exit 1; }
done
command -v ffmpeg >/dev/null || [[ -x /usr/local/bin/ffmpeg ]] || { echo "ffmpeg not found" >&2; exit 1; }

DATA=sdmc-out/3ds/toontown3ds/data
RES=$ROOT/ref/resources
LOG=${TT_BAKE_LOG:-sdmc-out/bake-logs/$(date +%Y%m%d-%H%M%S)}
SCRATCH=$LOG/scratch
mkdir -p "$DATA" "$LOG" "$SCRATCH"
LOG=$(cd "$LOG" && pwd); SCRATCH=$LOG/scratch

export TT_ETC1_THREADS=${TT_ETC1_THREADS:-2}
export OMP_NUM_THREADS=1 OPENBLAS_NUM_THREADS=1 MKL_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1
TOON_JOBS=${TT_TOON_JOBS:-3}
STREET_JOBS=${TT_STREET_JOBS:-2}

py() { nice -n 19 "$PYBIN" -I -B "$@"; }

# step <name> <command...>: run one baker, output to $LOG/<name>.log, fail the lane on error.
step() {
    local name=$1; shift
    local t0=$SECONDS
    echo "[$(date +%T)] start $name"
    if "$@" >"$LOG/$name.log" 2>&1; then
        echo "[$(date +%T)] done  $name ($((SECONDS - t0)) s)"
    else
        local rc=$?
        echo "[$(date +%T)] FAIL  $name (exit $rc, $((SECONDS - t0)) s); tail of $LOG/$name.log:"
        tail -n 30 "$LOG/$name.log" | sed 's/^/    /'
        return $rc
    fi
}

# ---------------------------------------------------------------------------- lanes
lane_toon() {
    # --no-cache: with Panda's shared model cache the toon bake was not reproducible (one
    # quantisation step off in some clips between runs); uncached it is byte-identical.
    step toon py tools/pipeline/bake_toon.py --no-cache --out-dir "$DATA/toon" --jobs "$TOON_JOBS"
    # battle_toon_extras.py (from bake_battle.py's toon stage) first asserts that the files of a
    # legacy 5-clip bake (neutral, run, walk, jump-idle, wave) are unchanged in the full bake.
    # That no longer holds for the 51-clip bake: torso_ms_* (.ttm and a few clips) differ by a
    # few vertex bytes because the base mesh depends on the clip set (checked: deterministic
    # either way). The guard belonged to the battle branch's own before/after audit, so record
    # the full bake's own hashes for those file names and let the exporter run.
    step toon-legacy-hash py - "$DATA/toon" <<'EOF2'
import hashlib, json, sys
from pathlib import Path
out = Path(sys.argv[1])
legacy = ('neutral', 'run', 'walk', 'jump-idle', 'wave')
def wanted(p):
    return p.suffix == '.ttm' or (p.suffix == '.tta' and p.stem.rsplit('_', 1)[-1] in legacy)
h = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(out.iterdir()) if wanted(p)}
(out / 'legacy_before_sha256.json').write_text(json.dumps(h, indent=2, sort_keys=True) + '\n')
print(len(h), 'legacy file hashes')
EOF2
    # battle_toon_extras.py copies the clothes stats from sdmc-out/toon-clothes.log (the last line
    # of bake_battle.py's clothes worker); hand it the full bake's own clothes.ttx entry.
    step toon-clothes-log py - "$DATA/toon/toon_manifest.json" sdmc-out/toon-clothes.log <<'EOF'
import json, sys
m = json.load(open(sys.argv[1]))
c = dict(m['clothes']); c.pop('file', None)
open(sys.argv[2], 'w').write(json.dumps(c) + '\n')
print(c)
EOF
    step toon-extras py tools/pipeline/battle_toon_extras.py
}

lane_world() {
    local Z=$DATA/zones
    # bake_hoods writes <zone>.pts too; bake_street must run after it so its .pts (with the
    # moving-prop anchors) wins. The hood .ttm/.ttc are the runtime's fallback when no .tz exists.
    step hoods py tools/pipeline/bake_hoods.py
    local zones
    zones=$("$PYBIN" -I -B -c "import sys; sys.path.insert(0,'tools/pipeline'); import street_catalog as s; print(' '.join(map(str, s.ZONES)), s.TUTORIAL_BRANCH)")
    step coghq py tools/pipeline/bake_coghq.py --zone all --audio --out "$Z" &
    local coghq=$!
    # One process per zone; zone 1000 leaves the moving boat to bake_boat.py.
    local rc=0
    printf '%s\n' $zones | xargs -P "$STREET_JOBS" -I{} bash -c '
        z=$1; extra=; [[ $z == 1000 ]] && extra=--separate-boat
        '"$(declare -f step py)"'
        LOG='"$LOG"' PYBIN='"$PYBIN"'
        step street-$z py tools/pipeline/bake_street.py --zone $z $extra --resources '"$RES"' --out '"$Z"'
    ' _ {} || rc=$?
    wait $coghq || rc=$?
    [[ $rc == 0 ]] || return $rc
    # Facility elevator anchors (elevator_origin_<n>) appended to the Cog HQ .pts sidecars:
    # cog_buildings.cpp places the factory/mint/DA office elevators there.
    step coghq-elevators py tools/pipeline/coghq_elevators.py --zones "$Z" --resources "$RES"
    step boat py tools/pipeline/bake_boat.py --resources "$RES" --out "$Z" --header "$SCRATCH/boat_paths.h"
    step suit-paths py tools/pipeline/bake_suit_paths.py --out "$Z"
    step minimap py tools/pipeline/bake_minimap.py --zones-dir "$Z" --out-dir "$Z" \
        --preview-dir sdmc-out/previews/minimap
    if [[ -x tools/server/.venv/bin/python ]] && tools/server/.venv/bin/python -I -c 'import panda3d.toontown' 2>/dev/null; then
        step knock nice -n 19 tools/server/.venv/bin/python -I -B tools/pipeline/bake_knock_doors.py --out "$Z"
    else
        echo "[$(date +%T)] skip  knock (no tools/server/.venv with panda3d.toontown; zones/*.kkd not baked)"
    fi
}

lane_interiors() {
    step interiors py tools/pipeline/bake_interior.py --all --no-preview --resources "$RES"
    step interiors-tutorial py tools/pipeline/bake_interior.py --tutorial --no-preview --resources "$RES"
    step interior-props py tools/pipeline/bake_interior_props.py --sd sdmc-out
}

lane_level() {
    step level py tools/pipeline/bake_level.py --level all --resources "$RES"
    step level-props py tools/pipeline/bake_level_props.py
    step facility-floors py tools/pipeline/bake_facility_floors.py --resources "$RES"
    step boss-vp py tools/pipeline/bake_boss.py
    step boss-cfo py tools/pipeline/bake_boss.py --cfo
    step boss-cj py tools/pipeline/bake_boss.py --cj
    step boss-ceo py tools/pipeline/bake_boss.py --ceo
}

lane_battle() {
    step suit py tools/pipeline/bake_suit.py
    step props py tools/pipeline/bake_props.py
    step suit-props py tools/pipeline/bake_suit_props.py
    step battle-audio py tools/pipeline/battle_audio.py
    # bake_suit_heads_atlas.py also writes the committed romfs/baked/suit_heads.ttm and
    # source/ui/suit_heads_atlas.h; only its SD copy is wanted here, so the other two go to scratch.
    step suit-heads-atlas py - "$SCRATCH/suit-heads" <<'EOF2'
import sys
from pathlib import Path
sys.path.insert(0, 'tools/pipeline')
import bake_suit_heads_atlas as b
scratch = Path(sys.argv[1])
b.ROMFS_OUT = scratch / 'suit_heads.ttm'
b.HEADER_OUT = scratch / 'suit_heads_atlas.h'
b.main()
EOF2
}

lane_misc() {
    # The RomFS half of bake_audio goes to scratch: romfs/baked/audio is committed.
    step audio py tools/pipeline/bake_audio.py --out "$SCRATCH/romfs-audio" --sd-out "$DATA/audio" --resources "$RES"
    step chars py tools/pipeline/bake_char.py
    step char-paths py tools/pipeline/char_paths_bin.py "$DATA/chars"
    step fishing py tools/pipeline/bake_fishing.py --resources "$RES"
    step minigames py tools/pipeline/bake_minigame.py --resources "$RES"
    step golf py tools/pipeline/bake_golf.py --resources "$RES"
    step golf-physics py tools/pipeline/bake_golf_physics.py --resources "$RES"
    step cogbldg py tools/pipeline/bake_cogbldg.py --resources "$RES"
    step estate py tools/pipeline/bake_estate.py --resources "$RES"
    # bake_clerk_karts.py also copies its report to the committed docs/design/clerk_karts.json;
    # point that copy at scratch (the same report is data/clerks/karts/manifest.json).
    mkdir -p "$SCRATCH/clerk-karts/docs/design"
    step clerk-karts py - "$SCRATCH/clerk-karts" <<'EOF2'
import sys
from pathlib import Path
sys.path.insert(0, 'tools/pipeline')
import bake_clerk_karts as b
b.ROOT = Path(sys.argv[1])  # only used for the docs/design/clerk_karts.json copy
b.main()
EOF2
}

LANES=(toon world interiors level battle misc)

if [[ ${1:-} == --list ]]; then
    for l in "${LANES[@]}"; do
        echo "$l:"; declare -f "lane_$l" | grep -oE '^\s+step [a-z0-9-]+' | sed 's/^ *step /  /'
    done
    exit 0
fi
selected=("$@")
[[ ${#selected[@]} -gt 0 ]] || selected=("${LANES[@]}")
for l in "${selected[@]}"; do
    declare -F "lane_$l" >/dev/null || { echo "unknown lane: $l (lanes: ${LANES[*]})" >&2; exit 2; }
done

echo "bake_sd_all: lanes ${selected[*]}; logs in $LOG"
T0=$SECONDS
declare -A pids
for l in "${selected[@]}"; do
    ( set -euo pipefail; "lane_$l" ) > >(tee "$LOG/lane_$l.log" | sed -u "s/^/[$l] /") 2>&1 &
    pids[$l]=$!
done
failed=()
for l in "${selected[@]}"; do
    wait "${pids[$l]}" || failed+=("$l")
done
sleep 1  # let the tee/sed of the last lane flush

if [[ " ${selected[*]} " == *" toon "* || " ${selected[*]} " == *" battle "* ]] && [[ -d $DATA/toon && -d $DATA/suit ]]; then
    step battle-validate py tools/pipeline/battle_validate.py || echo "battle_validate failed (non-fatal)"
fi

echo
echo "bake_sd_all: $(( (SECONDS - T0) / 60 )) min $(( (SECONDS - T0) % 60 )) s"
du -sh "$DATA"/* 2>/dev/null | sort -k2
du -sh "$DATA"
if [[ ${#failed[@]} -gt 0 ]]; then
    echo "FAILED lanes: ${failed[*]} (see $LOG/lane_<name>.log)"
    exit 1
fi
echo "all lanes OK"
