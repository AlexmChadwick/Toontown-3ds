"""Render a Pick-A-Toon style dog bust for the banner toon plane."""

from __future__ import annotations

from pathlib import Path

import numpy as np
from panda3d.core import ColorAttrib, NodePath, OrthographicLens, PNMImage, WindowProperties, loadPrcFileData
from direct.actor.Actor import Actor

from scene_common import ALL_COLORS, configure_panda

HEAD_COLOR_IDX = 11
PORTRAIT_SIZE = 384


def _remove_all(root: NodePath, pattern: str) -> None:
    for child in root.findAllMatches(pattern):
        child.removeNode()


def build_dog_bust(loader, resources: Path) -> NodePath:
    configure_panda(resources)
    actor = Actor(
        {"head": "phase_3/models/char/tt_a_chr_dgm_shorts_head_500.bam"},
        {"head": {"neutral": "phase_3/models/char/tt_a_chr_dgm_shorts_head_neutral.bam"}},
    )
    actor.pose("neutral", 28, "head")
    head = actor.getPart("head")

    muzz = loader.loadModel(str(resources / "phase_3/models/char/dogMM_Shorts-headMuzzles-500.bam"))
    joint = head.find("**/joint_head")
    muzz.reparentTo(joint if not joint.isEmpty() else head)

    for expr in ("surprise", "angry", "sad", "laugh"):
        _remove_all(head, f"**/muzzle*{expr}")
    smile = head.find("**/muzzle*smile")
    if not smile.isEmpty():
        smile.show()

    eyes = head.find("**/eyes*")
    if not eyes.isEmpty():
        eyes.setColorOff(1)
        tex = loader.loadTexture(
            str(resources / "phase_3/maps/eyes.jpg"),
            str(resources / "phase_3/maps/eyes_a.rgb"),
        )
        eyes.setTexture(tex, 1)

    rgb = ALL_COLORS[HEAD_COLOR_IDX]
    ears = head.find("**/ears")
    if not ears.isEmpty():
        # Ear geom has no texture and black vertex colors; flat tint matches the head.
        ears.setAttrib(ColorAttrib.makeFlat((*rgb, 1.0)))
        ears.setLightOff(1)
    for part in head.findAllMatches("**/+GeomNode"):
        name = part.getName().lower()
        if "eye" in name or "pupil" in name or "lash" in name:
            part.setColorOff(1)
            continue
        if any(k in name for k in ("head", "ear", "nose")) or name == "":
            part.setColorScale(*rgb, 1.0)
            part.setLightOff(1)
        elif "muzzle" in name:
            part.setColorScale(0.83, 0.5, 0.3, 1.0)
            part.setLightOff(1)

    root = NodePath("bust")
    head.reparentTo(root)
    root.setH(180)
    root.setP(-4)
    return root


def render_portrait_rgba(resources: Path) -> np.ndarray:
    from direct.showbase.ShowBase import ShowBase

    configure_panda(resources)
    loadPrcFileData("", "load-display pandagl")
    loadPrcFileData("", "show-frame-rate-meter 0")

    shot = PNMImage()

    class Bake(ShowBase):
        def __init__(self):
            super().__init__()
            self.win.requestProperties(WindowProperties.size(PORTRAIT_SIZE, PORTRAIT_SIZE))
            self.setBackgroundColor(0, 0, 0, 0)
            self.win.setClearColorActive(True)
            self.win.setClearColor((0, 0, 0, 0))

            bust = build_dog_bust(self.loader, resources)
            bust.reparentTo(self.render)
            bust.setScale(3.2)
            bust.setY(0.35)

            self.render.setLightOff(1)
            for g in self.render.findAllMatches("**/+GeomNode"):
                g.setShaderOff(1)
                g.setLightOff(1)

            lens = OrthographicLens()
            lens.setFilmSize(7.2, 7.2)
            self.cam.node().setLens(lens)
            self.camera.setPos(0, -30, 0.35)
            self.camera.lookAt(0, 0, 0.35)

            for _ in range(20):
                self.graphicsEngine.renderFrame()
            self.win.getScreenshot(shot)

    Bake()
    w, h = shot.getXSize(), shot.getYSize()
    rgba = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            c = shot.getXelA(x, y)
            rgba[y, x] = [
                round(c[0] * 255),
                round(c[1] * 255),
                round(c[2] * 255),
                round(c[3] * 255),
            ]
    # Trim empty margins
    mask = rgba[:, :, 3] > 8
    if mask.any():
        ys, xs = np.where(mask)
        pad = 4
        y0, y1 = max(0, ys.min() - pad), min(h - 1, ys.max() + pad)
        x0, x1 = max(0, xs.min() - pad), min(w - 1, xs.max() + pad)
        rgba = rgba[y0 : y1 + 1, x0 : x1 + 1]
    return rgba
