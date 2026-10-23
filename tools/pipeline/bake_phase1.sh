#!/usr/bin/env bash
# Bake the Phase 1 RomFS asset set from ref/resources into romfs/baked/ (gitignored).
set -euo pipefail
cd "$(dirname "$0")/../.."
PY=.venv/bin/python
R=ref/resources
OUT=romfs/baked
mkdir -p "$OUT"
$PY -I tools/pipeline/bake_model.py $R/phase_4/models/neighborhoods/toontown_central.bam $OUT/toontown_central.ttm --max-tex 256
$PY -I tools/pipeline/bake_model.py $R/phase_4/models/modules/gazebo.bam $OUT/gazebo.ttm
$PY -I tools/pipeline/bake_model.py $R/phase_3.5/models/props/mailbox_TT.bam $OUT/mailbox_TT.ttm
$PY -I tools/pipeline/bake_toon.py $OUT/toon_dog_m.ttm --head dls --torso ms --legs m --lod 500
ls -l "$OUT"
