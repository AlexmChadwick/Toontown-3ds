#!/usr/bin/env python3
"""Bake the stock Cog-building shell, elevator, and phase-7 floor modules.

The output is deliberately a small streamed set: one department shell, one elevator,
and three reusable floor rooms.  The source models are read-only; all generated files
land under sdmc-out/3ds/toontown3ds/data/cogbldg/.
"""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

MAX_TRIS = 15000
MAX_BATCHES = 150


def resources_default():
    candidates = [ROOT / "ref/resources", Path("/Users/alexchadwick/toontown-3ds/ref/resources")]
    for candidate in candidates:
        if (candidate / "phase_7").exists():
            return candidate
    return candidates[0]


def bake_one(base, source, out):
    import extract
    import ttm
    model = base.loader.loadModel(str(source))
    if model.isEmpty():
        raise FileNotFoundError(source)
    ex = extract.Extractor(max_tex=128)
    ex.add(model)
    batches = ex.batches()
    stats = ttm.write(str(out), ex.textures, batches)
    model.removeNode()
    if stats["triangles"] > MAX_TRIS or stats["batches"] > MAX_BATCHES:
        raise RuntimeError(f"{out}: budget exceeded: {stats}")
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", default=str(resources_default()))
    ap.add_argument("--out", default=str(ROOT / "sdmc-out/3ds/toontown3ds/data/cogbldg"))
    args = ap.parse_args()

    from panda3d.core import loadPrcFileData
    loadPrcFileData("", "window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none")
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    resources = Path(args.resources)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sources = {
        "elevator": resources / "phase_11/models/lawbotHQ/LB_ElevatorScaled.bam",
        "floor_0": resources / "phase_7/models/modules/suit_interior.bam",
        "floor_1": resources / "phase_7/models/modules/cubicle_room.bam",
        "floor_2": resources / "phase_7/models/modules/boss_suit_office.bam",
    }
    # The same shell geometry is recoloured by the server department at runtime; keeping
    # four named copies makes the SD contract explicit and permits department overrides later.
    sources.update({f"shell_{dept}": sources["floor_0"] for dept in ("c", "l", "m", "s")})
    manifest = {"sources": {}, "budgets": {"max_triangles": MAX_TRIS, "max_batches": MAX_BATCHES}}
    for stem, source in sources.items():
        path = out / f"{stem}.ttm"
        stats = bake_one(base, source, path)
        manifest["sources"][stem] = {"model": str(source), **stats}
        print(json.dumps({"file": str(path), **stats}))
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    base.destroy()


if __name__ == "__main__":
    main()
