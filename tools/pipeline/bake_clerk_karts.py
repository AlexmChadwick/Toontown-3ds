"""Bake bounded kart-shop preview components using the pinned Kart methods.

Run Python -I -B. No files in ref or .venv are modified. Runtime composes the
actual nine-field KartDNA from one body and up to six equipped components.
"""

import ast
import hashlib
import json
import os
from pathlib import Path
import sys

os.environ["TT_ETC1_THREADS"] = "3"
os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["OMP_NUM_THREADS"] = "1"
ROOT = Path(__file__).resolve().parents[2]
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / "ref" if (ROOT / "ref/open-toontown/toontown").is_dir() else ROOT / "ref/ref"
sys.path.insert(0, str(ROOT / "tools/pipeline"))
sys.path.insert(0, str(ROOT / "tools/clerks_test"))
from upstream import KartDNA
import extract
import ttm
from panda3d.core import NodePath, BamCache, loadPrcFileData
import panda3d.core as core

loadPrcFileData(
    "",
    "window-type none\naudio-library-name null\nmodel-cache-dir\nnotify-level error\ntextures-power-2 none\nmodel-path "
    + str(REF / "resources"),
)
from direct.showbase.ShowBase import ShowBase

OUT = ROOT / "sdmc-out/3ds/toontown3ds/data/clerks/karts"


def main():
    base = ShowBase()
    BamCache.getGlobalPtr().setActive(False)
    source = ast.parse(
        (REF / "open-toontown/toontown/racing/Kart.py").read_text()
    )
    cls = next(
        node
        for node in source.body
        if isinstance(node, ast.ClassDef) and node.name == "Kart"
    )
    methods = [
        node
        for node in cls.body
        if isinstance(node, ast.FunctionDef)
        and node.name
        in (
            "__applyEngineBlock",
            "__applySpoiler",
            "__applyFrontWheelWells",
            "__applyBackWheelWells",
            "__applyRims",
            "__applyDecals",
        )
    ]
    small = ast.ClassDef(
        name="Kart", bases=[], keywords=[], body=methods, decorator_list=[]
    )

    class ResourceLoader:
        def loadModel(self, path):
            return base.loader.loadModel(
                str(REF / "resources" / (path + ".bam"))
            )

        def loadTexture(self, path, alpha=None):
            filename = str(REF / "resources" / path)
            if alpha:
                return base.loader.loadTexture(
                    filename, str(REF / "resources" / alpha)
                )
            return base.loader.loadTexture(filename)

    namespace = dict(vars(core), **vars(KartDNA), loader=ResourceLoader())
    exec(
        compile(
            ast.fix_missing_locations(ast.Module(body=[small], type_ignores=[])),
            "pinned Kart attachment methods",
            "exec",
        ),
        namespace,
    )
    Helper = namespace["Kart"]
    OUT.mkdir(parents=True, exist_ok=True)
    report = {"version": 1, "models": {}, "worst_resident": {}}
    for body in range(3):
        for accessory in [-1] + list(range(65)):
            root = NodePath("kart-preview")
            path = (
                REF
                / "resources"
                / (KartDNA.getKartModelPath(body, 1) + ".bam")
            )
            model = base.loader.loadModel(str(path))
            model.reparentTo(root)
            model.setH(180)
            model.setZ(0.025)
            root.setScale(2)
            helper = Helper()
            helper.geom = {0: model}
            helper.pitchNode = {0: model.find("**/suspensionNode")}
            helper.kartDNA = [body] + [-1] * 8
            helper.kartAccessories = {}
            helper.texCount = 1
            temp = NodePath("temp")
            helper.accGeomScale = temp.getScale(helper.pitchNode[0]) * 2
            temp.removeNode()
            before = list(root.findAllMatches("**/+GeomNode"))
            skip = []
            if accessory < 0:
                for name in (
                    "leftFrontWheelRim",
                    "rightFrontWheelRim",
                    "leftRearWheelRim",
                    "rightRearWheelRim",
                    "hoodDecal",
                    "leftSideDecal",
                    "rightSideDecal",
                ):
                    model.find("**/" + name).hide()
                name = f"kart_{body}_body"
            else:
                field = KartDNA.getAccessoryType(accessory)
                helper.kartDNA[field] = accessory
                method = {
                    3: "EngineBlock",
                    4: "Spoiler",
                    5: "FrontWheelWells",
                    6: "BackWheelWells",
                    7: "Rims",
                    8: "Decals",
                }[int(field)]
                getattr(helper, "_Kart__apply" + method)()
                keep = []
                if field == 7:
                    keep = [
                        model.find("**/" + name)
                        for name in (
                            "leftFrontWheelRim",
                            "rightFrontWheelRim",
                            "leftRearWheelRim",
                            "rightRearWheelRim",
                        )
                    ]
                elif field == 8:
                    keep = [
                        model.find("**/" + name)
                        for name in ("hoodDecal", "leftSideDecal", "rightSideDecal")
                    ]
                skip = [
                    node
                    for node in before
                    if not any(part == node or part.isAncestorOf(node) for part in keep)
                ]
                name = f"kart_{body}_accessory_{accessory}"
            exporter = extract.Extractor(
                max_tex=128,
                tint_rules=(
                    ("chasse", 1),
                    ("vertex", 2),
                    ("hoodDecal", 2),
                    ("*SideDecal", 2),
                ),
            )
            exporter.add(root, skip_under=skip)
            batches = exporter.batches()
            import numpy as np

            for batch in batches:
                used = np.unique(batch.indices)
                batch.verts = batch.verts[used]
                batch.indices = np.searchsorted(used, batch.indices)
            if not batches:
                raise ValueError(name + ": no source geometry")
            stats = ttm.write(str(OUT / (name + ".ttm")), exporter.textures, batches)
            if (
                stats["triangles"] > 15000
                or stats["batches"] > 150
                or stats["geometry_bytes"] > 512 * 1024
                or stats["texture_bytes"] > 1024 * 1024
            ):
                raise ValueError(name + ": preview budget exceeded")
            report["models"][name] = stats
            print(
                name,
                stats["triangles"],
                stats["geometry_bytes"],
                stats["texture_bytes"],
                flush=True,
            )
            root.removeNode()
        models = report["models"]
        worst = dict(models[f"kart_{body}_body"])
        for key in ("geometry_bytes", "texture_bytes", "triangles", "batches"):
            value = models[f"kart_{body}_body"][key]
            for category in (3, 4, 5, 6, 7, 8):
                value += max(
                    models[f"kart_{body}_accessory_{index}"][key]
                    for index in KartDNA.AccessoryTypeDict[category]
                )
            worst[key] = value
        if (
            worst["geometry_bytes"] > 512 * 1024
            or worst["texture_bytes"] > 1024 * 1024
            or worst["triangles"] > 15000
            or worst["batches"] > 150
        ):
            raise ValueError("Combined kart preview over budget")
        report["worst_resident"][str(body)] = {
            key: worst[key]
            for key in ("geometry_bytes", "texture_bytes", "triangles", "batches")
        }
    report["sha256"] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(OUT.glob("*.ttm"))
    }
    (OUT / "manifest.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    (ROOT / "docs/design/clerk_karts.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    base.destroy()


if __name__ == "__main__":
    main()
