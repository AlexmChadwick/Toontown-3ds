#!/usr/bin/env python3
"""Bake compact Cashbot Mint and Lawbot Office room props."""
import argparse, json, struct
from pathlib import Path
import sys
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
def resources_default():
    return Path("/Users/alexchadwick/toontown-3ds/ref/resources")
def bake(base, source, out, node=None):
    import extract, ttm
    model = base.loader.loadModel(str(source))
    if node:
        model = model.find("**/" + node)
    if model.isEmpty(): raise FileNotFoundError(source)
    from panda3d.core import TextureAttrib
    for node in model.findAllMatches("**/+GeomNode"):
        for i in range(node.node().getNumGeoms()):
            node.node().setGeomState(i, node.node().getGeomState(i).removeAttrib(TextureAttrib.getClassType()))
    ex = extract.Extractor(max_tex=96); ex.add(model)
    stats = ttm.write(str(out), ex.textures, ex.batches())
    model.removeNode()
    if stats["triangles"] > 30000 or stats["batches"] > 256: raise RuntimeError(stats)
    return stats


def bake_collision(base, source, out):
    import collide
    model = base.loader.loadModel(str(source))
    if model.isEmpty(): raise FileNotFoundError(source)
    stats = collide.extract(model, fallback_floor=True).write(str(out), cell_size=8.0,
                                                               max_bytes=1024 * 1024)
    model.removeNode()
    return stats
def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--resources", default=str(resources_default()))
    ap.add_argument("--out", default=str(ROOT / "sdmc-out/3ds/toontown3ds/data"))
    a = ap.parse_args()
    from panda3d.core import loadPrcFileData
    loadPrcFileData("", "window-type none\naudio-library-name null\ntextures-power-2 pad")
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase(); r = Path(a.resources); root = Path(a.out)
    mint, stage = root/"mint", root/"stage"; mint.mkdir(parents=True, exist_ok=True); stage.mkdir(parents=True, exist_ok=True)
    manifest = {"mint": {}, "stage": {}, "entities": {}}
    mint_src = r/"phase_10/models/cashbotHQ"
    stage_src = r/"phase_11/models/lawbotHQ"
    for i in range(3, 32):
        p = mint_src/f"ZONE{i:02d}a.bam"
        if p.exists():
            manifest["mint"][str(i)] = {"source": str(p), **bake(base, p, mint/f"room_{i:02d}.ttm")}
            manifest.setdefault("collision", {}).setdefault("mint", {})[str(i)] = bake_collision(
                base, p, mint/f"room_{i:02d}.ttc")
    for index, name in enumerate(("LB_Zone03a.bam","LB_Zone04a.bam","LB_Zone7a.bam","LB_Zone7av2.bam","LB_Zone08a.bam","LB_Zone13a.bam","LB_Zone22a.bam")):
        p = stage_src/name
        if p.exists():
            target = stage / f"room_{index:02d}.ttm"
            manifest["stage"][str(index)] = {"source": str(p), **bake(base, p, target)}
            manifest.setdefault("collision", {}).setdefault("stage", {})[name] = bake_collision(
                base, p, stage/f"room_{index:02d}.ttc")
    props = {"gear": mint_src/"MintGear.bam", "shelf": mint_src/"shelf_A1.bam",
             "laser": stage_src/"LB_wall_panel.bam", "camera": stage_src/"LB_torch_lampA.bam"}
    for name, p in props.items():
        manifest["entities"][name] = {"source": str(p), **bake(base, p, (mint if name in ("gear","shelf") else stage)/(name+".ttm"))}
    for out, magic, ids in ((mint, b"MNT1", range(26)), (stage, b"STG1", (0,1,2,3,4,5,6,7,8,9,10,11,100,101,102,103,104,105))):
        with (out/"room_spec.bin").open("wb") as f:
            f.write(magic); f.write(struct.pack("<HH", 1, len(tuple(ids))))
            for n in ids: f.write(struct.pack("<HBB", n, 0 if n == 0 else 1, 1 if n in (2, 17, 100, 105) else 0))
    (root/"mintoffice_manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    base.destroy()
if __name__ == "__main__": main()
