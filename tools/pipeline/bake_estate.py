#!/usr/bin/env python3
"""Bake compact estate scenes and a Doodle placeholder for the 3DS client."""
import argparse, json, struct
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

def bake(base, source, out):
    import extract, ttm, tz, collide
    model = base.loader.loadModel(str(source))
    if model.isEmpty():
        raise FileNotFoundError(source)
    from panda3d.core import TextureAttrib
    for node in model.findAllMatches("**/+GeomNode"):
        for i in range(node.node().getNumGeoms()):
            node.node().setGeomState(i, node.node().getGeomState(i).removeAttrib(TextureAttrib.getClassType()))
    ex = extract.Extractor(max_tex=96)
    ex.add(model)
    stats = ttm.write(str(out), ex.textures, ex.batches())
    blob = Path(out).read_bytes()
    empty_collision, _ = collide.CollisionMesh().pack()
    tz.write(str(Path(out).with_suffix(".tz")), 20000, [{
        "name": Path(out).stem[:12], "zone": 20000, "vis": [0], "model": blob,
        "low_model": b"", "stats": stats,
        "low_stats": {"geometry_bytes": 0, "texture_bytes": 0, "batches": 0, "triangles": 0},
    }], [], [], empty_collision)
    model.removeNode()
    if stats["triangles"] > 30000 or stats["batches"] > 256:
        raise RuntimeError(f"{source}: {stats}")
    return stats

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", default="/Users/alexchadwick/toontown-3ds/ref/resources")
    ap.add_argument("--out", default=str(ROOT / "sdmc-out/3ds/toontown3ds/data/estate"))
    args = ap.parse_args()
    from panda3d.core import loadPrcFileData
    loadPrcFileData("", "window-type none\naudio-library-name null\ntextures-power-2 pad")
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    resources = Path(args.resources)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    src = resources / "phase_5.5/models/estate"
    manifest = {"models": {}, "cells": [], "version": 1}
    sources = [("terrain", src / "terrain.bam"), ("interior", src / "tt_m_ara_int_estateHouseA.bam"),
               ("house_0", src / "houseA.bam"), ("house_1", src / "houseB.bam"),
               ("house_2", src / "tt_m_ara_est_house_castle.bam"),
               ("house_3", src / "tt_m_ara_est_house_cupcake.bam"),
               ("house_4", src / "tt_m_ara_est_house_tepee.bam"),
               ("house_5", src / "tt_m_ara_est_house_tiki.bam"),
               ("furniture_0", src / "regular_bed.bam"), ("furniture_1", src / "chairA.bam"),
               ("furniture_2", src / "lampA.bam"), ("furniture_3", src / "FireplaceRound.bam")]
    for name, source in sources:
        if source.exists():
            target = out / (name + ".ttm")
            manifest["models"][name] = {"source": str(source), **bake(base, source, target)}
            manifest["cells"].append({"name": name, "model": target.name, "zone": 20000 + len(manifest["cells"])})
    pet = resources / "phase_4/models/char/TT_pets-mod.bam"
    if pet.exists():
        manifest["models"]["doodle"] = {"source": str(pet), **bake(base, pet, out / "doodle.ttm")}
    (out / "estate_spec.bin").write_bytes(b"EST1" + struct.pack("<HH", 1, len(manifest["cells"])) +
                                         b"".join(struct.pack("<HBB", c["zone"], 0, 1) for c in manifest["cells"]))
    (out / "estate_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    base.destroy()

if __name__ == "__main__":
    main()
