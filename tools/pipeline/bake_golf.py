#!/usr/bin/env python3
"""Bake Chip 'n Dale golf holes into compact TTM models and a runtime manifest.

The stock game has 36 HoleInfo entries but reuses the 18 terrain models for
alternate layouts. Each entry gets its own manifest record so the runtime can
honor par/max-swing/blocker selection without parsing Panda assets.
"""
import argparse
import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from panda3d.core import Filename, loadPrcFileData, Point3
from direct.showbase.ShowBase import ShowBase

import extract
import ttm

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "ref" / "ref" / "resources"
OUT_DEFAULT = ROOT / "sdmc-out" / "3ds" / "toontown3ds" / "data" / "golf"

HOLES = [
    (0, 3, 6, 18), (1, 3, 6, 1), (2, 3, 6, 2), (3, 2, 6, 3),
    (4, 3, 6, 4), (5, 3, 6, 2), (6, 3, 6, 6), (7, 3, 6, 7),
    (8, 3, 6, 8), (9, 3, 6, 9), (10, 3, 6, 10), (11, 3, 6, 11),
    (12, 3, 6, 12), (13, 3, 6, 13), (14, 3, 6, 14), (15, 3, 6, 15),
    (16, 3, 6, 16), (17, 3, 6, 17), (18, 3, 6, 18), (19, 3, 6, 1),
    (20, 3, 6, 2), (21, 3, 6, 3), (22, 3, 6, 4), (23, 3, 6, 5),
    (24, 3, 6, 6), (25, 3, 6, 7), (26, 3, 6, 8), (27, 3, 6, 9),
    (28, 3, 6, 10), (29, 3, 6, 11), (30, 3, 6, 12), (31, 4, 7, 13),
    (32, 3, 6, 14), (33, 3, 6, 15), (34, 3, 6, 16), (35, 4, 7, 17),
]


def locator(model, name, fallback):
    node = model.find("**/" + name)
    if node.isEmpty():
        return list(fallback)
    p = node.getPos()
    return [round(float(p.x), 3), round(float(p.y), 3), round(float(p.z), 3)]


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
    baked = {}
    for hole_id, par, max_swing, model_id in HOLES:
        if model_id not in baked:
            path = Path(args.resources) / "phase_6" / "models" / "golf" / ("hole%d.bam" % model_id)
            model = base.loader.loadModel(Filename.fromOsSpecific(str(path)))
            ex = extract.Extractor(max_tex=128)
            ex.add(model)
            stats = ttm.write(str(out / ("hole%d.ttm" % model_id)), ex.textures, ex.batches())
            baked[model_id] = {"stats": stats, "tee": locator(model, "tee0", [0, 0, 0.25]),
                               "cup": locator(model, "holebottom0", [0, 15, 0.25])}
            model.removeNode()
        rec = {"id": hole_id, "par": par, "maxSwing": max_swing, "model": "hole%d.ttm" % model_id,
               "physics": "golfGreen%d" % model_id, "tee": baked[model_id]["tee"],
               "cup": baked[model_id]["cup"], "blockers": []}
        records.append(rec)
    for name in ("golf_cart3", "putter"):
        path = Path(args.resources) / "phase_6" / "models" / "golf" / (name + ".bam")
        model = base.loader.loadModel(Filename.fromOsSpecific(str(path)))
        ex = extract.Extractor(max_tex=128)
        ex.add(model)
        ttm.write(str(out / (name + ".ttm")), ex.textures, ex.batches())
        model.removeNode()
    (out / "holes.json").write_text(json.dumps({"version": 1, "holes": records}, sort_keys=True, indent=2) + "\n")
    print(json.dumps({"holes": len(records), "models": len(baked), "equipment": 2, "out": str(out)}))


if __name__ == "__main__":
    main()
