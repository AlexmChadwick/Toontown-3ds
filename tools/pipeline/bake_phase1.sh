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
# Toon parts: every species/torso/legs at LOD 500 + 250 and the full clothing pack go to
# the SD set (sdmc-out/, not committed); the boot set (default dog dls/ms/m at LOD 500 and
# the small Make-A-Toon clothes.ttx) goes to RomFS. Textures are ETC1 (TT_ETC1 default on).
$PY -I tools/pipeline/bake_toon.py --out-dir sdmc-out/3ds/toontown3ds/data/toon --boot-dir $OUT/toon
ls -l "$OUT"
