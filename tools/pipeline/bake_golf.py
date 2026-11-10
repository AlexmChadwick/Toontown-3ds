#!/usr/bin/env python3
"""Bake Chip 'n Dale golf holes into compact TTM models and a runtime manifest.

The stock game has 36 HoleInfo entries but reuses the 18 terrain models for
alternate layouts with different blockers. Each entry bakes its own hole_<id>.ttm
(only its blockers, the barrier geoms hidden, as GolfHoleBase) and manifest record.
"""
import argparse
import ast
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import Filename, loadPrcFileData, Point3
from direct.showbase.ShowBase import ShowBase

import assemble
import extract
import ttm

ROOT = Path(__file__).resolve().parents[2]
# Main checkout: ref/resources; agent worktrees link the main ref/ as ref/ref.
RESOURCES = next((p for p in (ROOT / "ref" / "resources", ROOT / "ref" / "ref" / "resources")
                  if (p / "phase_6").exists()), ROOT / "ref" / "resources")
OUT_DEFAULT = ROOT / "sdmc-out" / "3ds" / "toontown3ds" / "data" / "golf"


def locator(model, name, fallback):
    node = model.find("**/" + name)
    if node.isEmpty():
        return list(fallback)
    p = node.getPos()
    return [round(float(p.x), 3), round(float(p.y), 3), round(float(p.z), 3)]


def load_hole_info(resources):
    """GolfGlobals.HoleInfo, read from the pinned upstream source."""
    golf_globals = Path(resources).parent / "open-toontown" / "toontown" / "golf" / "GolfGlobals.py"
    for node in ast.walk(ast.parse(golf_globals.read_text())):
        if isinstance(node, ast.Assign) and any(getattr(t, "id", "") == "HoleInfo" for t in node.targets):
            return ast.literal_eval(node.value)
    raise ValueError("HoleInfo not found in %s" % golf_globals)


def apply_blockers(model, info):
    """DistributedGolfHole.loadBlockers without golf-all-blockers: hardSurface's locator1,
    locator2, ... (up to the first gap) keep their children when the hole lists them and are
    removed otherwise. Returns the kept numbers."""
    keep = info.get("blockers", ())
    keep = {keep} if isinstance(keep, int) else set(keep)
    hard = model.find("**/hardSurface")
    num = 1
    while not hard.isEmpty():
        loc = hard.find("**/locator%d" % num)
        if loc.isEmpty():
            break
        if num in keep:
            loc.getChildren().wrtReparentTo(loc.getParent())
        else:
            loc.removeNode()
        num += 1
    return sorted(keep)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(OUT_DEFAULT))
    ap.add_argument("--resources", default=str(RESOURCES))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    loadPrcFileData("", "window-type none\naudio-library-name null\ntextures-power-2 none")
    base = ShowBase()
    records = []
    # GolfGlobals.HoleInfo: each of the 36 holes names its terrain model and the blocker
    # locators GolfHoleBase.loadBlockers keeps (the others are removed). Several holes share a
    # terrain with different blockers, so every hole bakes its own hole_<id>.ttm.
    hole_info = load_hole_info(args.resources)
    for hole_id in sorted(hole_info):
        info = hole_info[hole_id]
        model = base.loader.loadModel(Filename.fromOsSpecific(str(Path(args.resources) / info["terrainModel"])),
                                      noCache=True)
        keep = apply_blockers(model, info)
        # GolfHoleBase: the collision? barrier geoms are hidden (golfBarrier.hide()).
        for barrier in model.findAllMatches("**/collision?"):
            barrier.stash()
        # The holes' golf_flowers and tree sprites are BillboardEffect cards (frozen, edge-on sticks).
        assemble.cross_billboards(model)
        ex = extract.Extractor(max_tex=128)
        ex.add(model)
        name = "hole_%d.ttm" % hole_id
        stats = ttm.write(str(out / name), ex.textures, ex.batches())
        model_id = int(Path(info["terrainModel"]).stem[len("hole"):])
        records.append({"id": hole_id, "par": info["par"], "maxSwing": info["maxSwing"], "model": name,
                        "physics": info["physicsData"], "terrain": model_id,
                        "tee": locator(model, "tee0", [0, 0, 0.25]),
                        "cup": locator(model, "holebottom0", [0, 15, 0.25]),
                        "blockers": keep, "triangles": stats["triangles"]})
        model.removeNode()
    for name in ("golf_cart3", "putter"):
        path = Path(args.resources) / "phase_6" / "models" / "golf" / (name + ".bam")
        model = base.loader.loadModel(Filename.fromOsSpecific(str(path)))
        ex = extract.Extractor(max_tex=128)
        ex.add(model)
        ttm.write(str(out / (name + ".ttm")), ex.textures, ex.batches())
        model.removeNode()
    (out / "holes.json").write_text(json.dumps({"version": 1, "holes": records}, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"holes": len(records), "equipment": 2, "out": str(out)}))


if __name__ == "__main__":
    main()
