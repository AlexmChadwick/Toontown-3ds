"""Render a Pick-A-Toon style dog bust for the banner toon plane.

The head is the stock dog-M model. Expression muzzles live in the separate
dogMM file (ToonHead.setupMuzzles); the neutral muzzle stays on the head and
is hidden so only the smile shows. Eyes use eyes.jpg plus eyes_a.rgb. The
portrait is stored top-row-first with a transparent background, the same
convention as the logo plane.
"""

from __future__ import annotations

from pathlib import Path

import numpy as np
from panda3d.core import (
    NodePath,
    OrthographicLens,
    PNMImage,
    TransparencyAttrib,
    WindowProperties,
    loadPrcFileData,
)
from direct.actor.Actor import Actor

from scene_common import ALL_COLORS, configure_panda

# ToonDNA head color 10: the sunny yellow used all over Pick-A-Toon.
HEAD_COLOR_IDX = 10
MUZZLE_COLOR = (0.83, 0.50, 0.30)
PORTRAIT_SIZE = 384


def _hide_named(root: NodePath, pattern: str) -> None:
    for child in root.findAllMatches(pattern):
        child.hide()


def build_dog_bust(loader, resources: Path) -> NodePath:
    configure_panda(resources)
    actor = Actor(
        {"head": "phase_3/models/char/tt_a_chr_dgm_shorts_head_500.bam"},
        {"head": {"neutral": "phase_3/models/char/tt_a_chr_dgm_shorts_head_neutral.bam"}},
    )
    # Frame 0 of neutral is the open-eyed rest pose. Later frames squint.
    actor.pose("neutral", 0, "head")
    head = actor.getPart("head")

    # Smile replaces the neutral muzzle. Parent to def_head so it shares the
    # head's model space (joint_head is not in this LOD).
    muzz = loader.loadModel(str(resources / "phase_3/models/char/dogMM_Shorts-headMuzzles-500.bam"))
    anchor = head.find("**/def_head")
    muzz.reparentTo(anchor if not anchor.isEmpty() else head)
    neutral = head.find("**/muzzle")
    if not neutral.isEmpty() and neutral.getName() == "muzzle":
        neutral.hide()
    for expr in ("surprise", "angry", "sad", "laugh"):
        _hide_named(head, f"**/muzzle*{expr}")

    eyes = head.find("**/eyes*")
    if not eyes.isEmpty():
        eyes.setColorOff(1)
        eyes.setTexture(
            loader.loadTexture(
                str(resources / "phase_3/maps/eyes.jpg"),
                str(resources / "phase_3/maps/eyes_a.rgb"),
            ),
            1,
        )
        eyes.setTransparency(TransparencyAttrib.MBinary)
        eyes.setDepthOffset(1)

    rgb = ALL_COLORS[HEAD_COLOR_IDX]
    for part in head.findAllMatches("**/+GeomNode"):
        name = part.getName().lower()
        # The pupil geom on this LOD has an empty name and a pupils texture.
        if "eye" in name or "pupil" in name or name == "":
            part.setColorOff(1)
            part.setDepthOffset(2 if name != "eyes" else 1)
            continue
        if "muzzle" in name:
            part.setColorScale(*MUZZLE_COLOR, 1.0)
        elif "nose" in name:
            part.setColorScale(0.12, 0.08, 0.06, 1.0)
        else:
            # Head, head-front and ears. Ears keep their vertex colors, so a
            # scale tints them without turning the mesh into a solid block.
            part.setColorScale(*rgb, 1.0)
        part.setLightOff(1)

    root = NodePath("bust")
    head.reparentTo(root)
    # The dog head faces +Y. Turn it toward the camera, which sits on -Y.
    root.setH(180)
    return root


def render_portrait_rgba(resources: Path) -> np.ndarray:
    from direct.showbase.ShowBase import ShowBase

    configure_panda(resources)
    # Before ShowBase, or the window is created without an alpha plane and the
    # clear color comes back opaque.
    loadPrcFileData("", "load-display pandagl")
    loadPrcFileData("", "framebuffer-alpha true")
    loadPrcFileData("", "alpha-bits 8")
    loadPrcFileData("", "color-bits 8 8 8")
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
            self.render.setLightOff(1)

            bounds = bust.getTightBounds()
            if not bounds:
                raise RuntimeError("dog bust has no geometry")
            lo, hi = bounds
            center_x = (lo.x + hi.x) * 0.5
            center_z = (lo.z + hi.z) * 0.5
            span = max(hi.x - lo.x, hi.z - lo.z) * 1.06
            lens = OrthographicLens()
            lens.setFilmSize(span, span)
            lens.setNear(0.1)
            lens.setFar(100.0)
            self.cam.node().setLens(lens)
            self.camera.setPos(center_x, -20.0, center_z)
            self.camera.lookAt(center_x, 0.0, center_z)

            for _ in range(20):
                self.graphicsEngine.renderFrame()
            self.win.getScreenshot(shot)

    Bake()
    if not shot.hasAlpha():
        raise RuntimeError("portrait screenshot has no alpha channel")
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
    # PNM y=0 is the bottom of the window, and that row is the top of the face
    # (eyes). Row 0 of this array is what the banner quads put at +Y.
    mask = rgba[:, :, 3] > 8
    if not mask.any():
        raise RuntimeError("portrait rendered fully transparent")
    ys, xs = np.where(mask)
    pad = 6
    y0, y1 = max(0, int(ys.min()) - pad), min(h - 1, int(ys.max()) + pad)
    x0, x1 = max(0, int(xs.min()) - pad), min(w - 1, int(xs.max()) + pad)
    cropped = rgba[y0 : y1 + 1, x0 : x1 + 1]
    # Banner quads show the last row of a top-row-first array at +Y (see the
    # logo loader). flipud puts the eyes, which sit at the start of this
    # screenshot, on that top edge.
    return np.flipud(cropped)
