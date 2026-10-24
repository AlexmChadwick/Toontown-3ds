#!/usr/bin/env python3
"""Bake the Sellbot Factory scene, entity props, and an offline level table.

All output is generated under sdmc-out/3ds/toontown3ds/data/factory/.  The level
table is intentionally binary: the client never imports or evaluates Python
level specs at runtime.
"""
import argparse
import json
import struct
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
MAX_TRIS = 30000
MAX_BATCHES = 256

ENTITIES = [
    (10039, 3, 1, -7.0, 29.0, 0.0),
    (30040, 3, 2, 0.0, 6.75, 0.0),
    (20081, 9, 4, 0.0, 0.0, 0.0),
    (20091, 10, 4, 0.0, 23.0, 0.0),
    (20060, 9, 5, 0.0, 0.0, 0.0),
    (20062, 10, 5, 0.0, 0.0, 0.0),
    (20023, 17, 6, 0.0, 0.0, 0.0),
]


def resources_default():
    for candidate in (ROOT / "ref/resources", Path("/Users/alexchadwick/toontown-3ds/ref/resources")):
        if (candidate / "phase_9").exists():
            return candidate
    return ROOT / "ref/resources"


def bake_one(base, source, out, node_name=None):
    import extract
    import ttm
    model = base.loader.loadModel(str(source))
    if model.isEmpty():
        raise FileNotFoundError(source)
    if node_name:
        selected = model.find("**/" + node_name)
        if selected.isEmpty():
            model.removeNode()
            raise FileNotFoundError(f"{source}: {node_name}")
        model = selected
    ex = extract.Extractor(max_tex=128)
    ex.add(model)
    stats = ttm.write(str(out), ex.textures, ex.batches())
    model.removeNode()
    if stats["triangles"] > MAX_TRIS or stats["batches"] > MAX_BATCHES:
        raise RuntimeError(f"{out}: budget exceeded: {stats}")
    return stats


def bake_collision(base, source, out, node_name=None):
    import collide
    model = base.loader.loadModel(str(source))
    if node_name:
        model = model.find("**/" + node_name)
    if model.isEmpty():
        raise FileNotFoundError(source)
    mesh = collide.extract(model, fallback_floor=True)
    stats = mesh.write(str(out), cell_size=8.0, max_bytes=1024 * 1024)
    model.removeNode()
    return stats


def write_level_table(out):
    path = out / "level_spec.bin"
    with path.open("wb") as f:
        f.write(b"FCT1")
        f.write(struct.pack("<HH", 1, len(ENTITIES)))
        for ent_id, zone, kind, x, y, z in ENTITIES:
            f.write(struct.pack("<IHBxfff", ent_id, zone, kind, x, y, z))
    return {"file": str(path), "version": 1, "entities": len(ENTITIES)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", default=str(resources_default()))
    ap.add_argument("--out", default=str(ROOT / "sdmc-out/3ds/toontown3ds/data/factory"))
    args = ap.parse_args()
    from panda3d.core import loadPrcFileData
    loadPrcFileData("", "window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none")
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    resources = Path(args.resources)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    sources = {
        "factory": resources / "phase_9/models/cogHQ/SellbotFactoryExterior.bam",
        "lift": resources / "phase_9/models/cogHQ/FactoryGearB.bam",
        "door": resources / "phase_9/models/cogHQ/CogDoor_Button.bam",
        "button": resources / "phase_9/models/cogHQ/CogDoor_Button.bam",
        "crate": resources / "phase_9/models/cogHQ/woodCrateB.bam",
        # The stock walk BAM is animation-only; use the factory gear prop as a
        # bounded placeholder until the native CChar animation path is wired.
        "goon": resources / "phase_9/models/cogHQ/FactoryGearB.bam",
        "stomper": resources / "phase_9/models/cogHQ/square_stomper.bam",
    }
    manifest = {"budgets": {"max_triangles": MAX_TRIS, "max_batches": MAX_BATCHES}, "models": {}}
    for stem, source in sources.items():
        manifest["models"][stem] = {"source": str(source), **bake_one(base, source, out / f"{stem}.ttm")}
    manifest["collision"] = bake_collision(base, sources["factory"], out / "factory.ttc")
    # Keep the runtime's cell contract explicit. The source factory is a single
    # authored scene; the table gives the visibility/entity partitioning.
    interior = resources / "phase_9/models/cogHQ/SelbotLegFactory.bam"
    for zone in (3, 4, 5, 6, 7, 8, 9, 10, 17, 19, 23, 24):
        target = out / f"cell_{zone}.ttm"
        manifest["models"][f"cell_{zone}"] = {"source": str(interior),
                                               **bake_one(base, interior, target, f"ZONE{zone:02d}")}
    manifest["level_spec"] = write_level_table(out)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    base.destroy()


if __name__ == "__main__":
    main()
