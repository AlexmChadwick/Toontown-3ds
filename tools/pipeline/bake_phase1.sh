#!/usr/bin/env bash
# Bake the RomFS boot asset set from ref/resources into romfs/baked/ (committed;
# re-run after baker changes). Larger sets go to sdmc-out/ (see the individual bakers).
set -euo pipefail
cd "$(dirname "$0")/../.."
PY=.venv/bin/python
R=ref/resources
DNA=$R/phase_4/dna
OUT=romfs/baked
mkdir -p "$OUT"
# Toontown Central playground, assembled from DNA.
$PY -I tools/pipeline/bake_zone.py $DNA/storage.dna $DNA/storage_TT.dna $DNA/storage_TT_sz.dna \
    --zone $DNA/toontown_central_sz.dna --out $OUT/ttc_sz.ttm
# Default toon body parts + animations.
$PY -I tools/pipeline/bake_toon.py --out-dir $OUT/toon --head dls --torso ms --legs m --lod 500
ls -l "$OUT"
