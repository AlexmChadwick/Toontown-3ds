"""Decode baked TTM bytes into Panda offscreen contact sheets. Python -I -B."""

from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pipeline"))
import preview_ttm
from panda3d.core import BamCache, NodePath

BamCache.getGlobalPtr().setActive(False)
from direct.showbase.RandomNumGen import RandomNumGen

base = preview_ttm.ShowBase()
base.setBackgroundColor(0.25, 0.35, 0.45, 1)
base.camLens.setFov(55)
base.camLens.setNearFar(1, 600)
assets = ROOT / "sdmc-out/3ds/toontown3ds/data/cogdo"
out = ROOT / "sdmc-out/cogdo/previews"
out.mkdir(parents=True, exist_ok=True)
for scene in ("shell", "suite", "barrel_room", "maze"):
    root = NodePath(scene)
    root.reparentTo(base.render)
    if scene == "maze":
        preview_ttm.load(str(assets / "maze_frame.ttm"), {}).reparentTo(root)
        parent = RandomNumGen(42)
        rng = RandomNumGen(parent)
        barriers = list(range(12))
        rng.shuffle(barriers)
        quadrants = list(range(1, 10))
        rng.shuffle(quadrants)
        for index, quadrant in enumerate(quadrants):
            node = preview_ttm.load(str(assets / f"maze_quadrant_{quadrant}.ttm"), {})
            node.reparentTo(root)
            node.setPos((index % 3 - 1) * 51, (index // 3 - 1) * 51, 0)
            node.setH(rng.randint(0, 3) * 90)
        base.camera.setPos(0, -45, 180)
        base.camera.lookAt(0, 0, 0)
    else:
        preview_ttm.load(str(assets / f"{scene}.ttm"), {}).reparentTo(root)
        if scene == "suite":
            for part in ("suite_cage", "suite_door"):
                preview_ttm.load(str(assets / f"{part}.ttm"), {}).reparentTo(root)
        low, high = root.getTightBounds()
        center = (low + high) / 2
        radius = (high - low).length() / 2
        base.camera.setPos(center + (radius * 0.8, -radius * 2, radius * 0.8))
        base.camera.lookAt(center)
    if scene == "barrel_room":
        base.camera.setPos(0, -78, 28)
        base.camera.lookAt(0, -30, 0)
    if scene == "suite":
        base.camera.setPos(0, -38, 22)
        base.camera.lookAt(5, 25, 7)
    for _ in range(3):
        base.graphicsEngine.renderFrame()
    assert base.win.saveScreenshot(str(out / f"{scene}.png"))
    root.removeNode()
print("Decoded Cogdo TTM texture/mip/index validation and previews: PASS")
base.destroy()
