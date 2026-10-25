"""Decode baked kart components with the independent TTM preview reader."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pipeline"))
from preview_ttm import ShowBase, load

base = ShowBase()
base.setBackgroundColor(0.4, 0.65, 0.85, 1)
for body in range(3):
    root = base.render.attachNewNode("kart")
    root.setX((body - 1) * 7)
    folder = ROOT / "sdmc-out/3ds/toontown3ds/data/clerks/karts"
    files = [f"kart_{body}_body.ttm"] + [
        f"kart_{body}_accessory_{item}.ttm" for item in (0, 11, 19, 27, 41, 55)
    ]
    for filename in files:
        load(
            str(folder / filename), {1: (0.7, 0.25, 0.8, 1), 2: (0.9, 0.9, 0.25, 1)}
        ).reparentTo(root)
base.camera.setPos(8, -20, 10)
base.camera.lookAt(0, 0, 1)
base.camLens.setFov(60)
base.camLens.setNearFar(0.1, 100)
base.graphicsEngine.renderFrame()
base.graphicsEngine.renderFrame()
base.win.saveScreenshot(str(ROOT / "sdmc-out/clerks/kart-preview.png"))
base.destroy()
