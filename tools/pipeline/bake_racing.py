#!/usr/bin/env python3
"""Bake the compact Goofy Speedway racing set for the 3DS client."""
import argparse
import json
import os
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

SOURCES = {
    "screwball_stadium": "RT_SpeedwayA",
    "rustic_raceway": "RT_RuralB",
    "city_circuit": "RT_UrbanA",
    "corkscrew_coliseum": "RT_SpeedwayB",
    "airborne_acres": "RT_RuralB2",
    "blizzard_boulevard": "RT_UrbanB",
    "screwball_stadium_rev": "RT_SpeedwayA",
    "rustic_raceway_rev": "RT_RuralB",
    "city_circuit_rev": "RT_UrbanA",
    "corkscrew_coliseum_rev": "RT_SpeedwayB",
    "airborne_acres_rev": "RT_RuralB2",
    "blizzard_boulevard_rev": "RT_UrbanB",
    "kart_default": "Kart1_Final",
    "kart_pad": "kartPad",
    "gag_pie": "pie_splat_1",
    "gag_panel": "gag_panel",
}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", default=str(ROOT / "ref/resources"))
    ap.add_argument("--out", default=str(ROOT / "sdmc-out/3ds/toontown3ds/data/racing"))
    args = ap.parse_args()
    for cand in (Path(args.resources), Path("/Users/alexchadwick/toontown-3ds/ref/resources")):
        if (cand / "phase_6").exists():
            args.resources = str(cand)
            break
    from panda3d.core import loadPrcFileData
    loadPrcFileData("", "window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none")
    from direct.showbase.ShowBase import ShowBase
    import extract
    import ttm
    import collide
    base = ShowBase()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    root = Path(args.resources) / "phase_6/models/karting"
    report = {"models": []}
    pad_source = base.loader.loadModel(str(root / "kartPad.bam"))
    pad_collision = collide.extract(pad_source, fallback_floor=True)
    for name, source in SOURCES.items():
        model = base.loader.loadModel(str(root / (source + ".bam")))
        if model.isEmpty():
            raise FileNotFoundError(source)
        ex = extract.Extractor(max_tex=128); ex.add(model)
        stats = ttm.write(str(out / (name + ".ttm")), ex.textures, ex.batches())
        if stats["triangles"] > 15000 or stats["batches"] > 150:
            raise RuntimeError(f"{name}: budget exceeded {stats}")
        report["models"].append({"name": name, **stats})
        if name.endswith(("stadium", "raceway", "circuit", "coliseum", "acres", "boulevard")):
            pad_collision.write(str(out / (name + ".ttc")), cell_size=8.0, max_bytes=256 * 1024)
        model.removeNode()
    (out / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))

if __name__ == "__main__":
    main()
