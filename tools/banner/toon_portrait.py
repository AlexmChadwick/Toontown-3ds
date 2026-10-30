"""Dog bust for the HOME Menu banner, rendered by the upstream toon code.

Pick-A-Toon builds the head with ToonHead.setupHead; a full toon (head plus
torso) is Toon.setDNA on the same ToonDNA. This bakes a stock long dog
(``dll``, light blue, neutral/happy) in the Make-A-Toon 3/4 pose: the
Make-A-Toon camera, and the Name-shop heading of 120. The portrait is an
RGBA image with a transparent background, stored so the last row is the top
of the head (the same convention as the logo plane).
"""

from __future__ import annotations

import builtins
import math
import sys
import types
from pathlib import Path

import numpy as np
from panda3d.core import (
    AmbientLight,
    DirectionalLight,
    PNMImage,
    PandaNode,
    Vec3,
    Vec4,
    loadPrcFileData,
)

# ToonDNA allColorsList index 17, the stock light blue.
HEAD_COLOR = 17
# MakeAToon.enter camera, and enterNameShop's 3/4 heading on the stage toon.
MAT_CAM_POS = (-5.7, -12.3501, 2.15)
MAT_CAM_HPR = (-24.8499, 2.73, 0.0)
MAT_CAM_FOV = 52.0
TOON_POS = (-1.62, -3.49, 0.0)
TOON_H = 120.0
PORTRAIT_SIZE = 640


def _install_otp_stub() -> None:
    """Stock panda3d has no panda3d.otp. Avatar only needs the nametag types."""
    if "panda3d.otp" in sys.modules:
        return
    otp = types.ModuleType("panda3d.otp")

    class _Any:
        def __init__(self, *args, **kwargs):
            pass

        def __getattr__(self, name):
            return lambda *args, **kwargs: None

    class NametagGroup(_Any):
        CCNormal = 1
        CCNoChat = 2
        CCFreeChat = 3
        CCSpeedChat = 4
        CCSuit = 5

        def getNametag3d(self):
            return PandaNode("nametag3d")

        def getNameIcon(self):
            return PandaNode("nameIcon")

    class Nametag(_Any):
        CName = 1
        CSpeech = 2

    otp.Nametag = Nametag
    otp.NametagGroup = NametagGroup
    otp.WhisperPopup = _Any
    otp.CMover = _Any
    for name, value in {
        "CFSpeech": 1,
        "CFThought": 2,
        "CFTimeout": 4,
        "CFPageButton": 8,
        "CFNoQuitButton": 16,
        "CFQuitButton": 32,
    }.items():
        setattr(otp, name, value)
    sys.modules["panda3d.otp"] = otp


def _pnm_rgba(img: PNMImage) -> np.ndarray:
    w, h = img.getXSize(), img.getYSize()
    rgba = np.empty((h, w, 4), dtype=np.uint8)
    for y in range(h):
        row = rgba[y]
        for x in range(w):
            c = img.getXelA(x, y)
            row[x, 0] = round(c[0] * 255)
            row[x, 1] = round(c[1] * 255)
            row[x, 2] = round(c[2] * 255)
            row[x, 3] = round(c[3] * 255)
    return rgba


def _screenshot(win) -> np.ndarray:
    img = PNMImage()
    if not win.getScreenshot(img):
        raise RuntimeError("toon portrait screenshot failed")
    if not img.hasAlpha():
        raise RuntimeError("toon portrait framebuffer has no alpha")
    return _pnm_rgba(img)


def _straighten_alpha(black: np.ndarray, white: np.ndarray) -> np.ndarray:
    """Recover straight color if the GL blend stored premultiplied edges.

    A straight-alpha buffer matches on black and white clears. A premultiplied
    edge is dark on black and bright on white; divide the black RGB by alpha.
    """
    out = black.copy()
    alpha = black[:, :, 3].astype(np.float32)
    partial = (alpha > 8.0) & (alpha < 248.0)
    if not partial.any():
        return out
    rgb_b = black[:, :, :3].astype(np.int16)
    rgb_w = white[:, :, :3].astype(np.int16)
    darker = (rgb_b + 12 < rgb_w).any(axis=2) & partial
    if int(darker.sum()) * 2 < int(partial.sum()):
        return out
    scale = np.maximum(alpha, 1.0) / 255.0
    recovered = np.clip(black[:, :, :3].astype(np.float32) / scale[:, :, None], 0, 255)
    out[:, :, :3] = np.where(partial[:, :, None], recovered, black[:, :, :3]).astype(np.uint8)
    return out


def _bleed_rgb(rgba: np.ndarray, passes: int = 4) -> None:
    """Copy opaque RGB into neighboring transparent pixels.

    Linear filtering of straight alpha against black RGB fringes a dark halo.
    The alpha stays 0; only the hidden color changes.
    """
    rgb = rgba[:, :, :3]
    solid = rgba[:, :, 3] > 16
    for _ in range(passes):
        acc = np.zeros(rgb.shape, dtype=np.uint16)
        weight = np.zeros(solid.shape, dtype=np.uint16)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            src_solid = np.roll(np.roll(solid, dy, axis=0), dx, axis=1)
            src_rgb = np.roll(np.roll(rgb, dy, axis=0), dx, axis=1)
            if dy == 1:
                src_solid[0, :] = False
            elif dy == -1:
                src_solid[-1, :] = False
            if dx == 1:
                src_solid[:, 0] = False
            elif dx == -1:
                src_solid[:, -1] = False
            take = (~solid) & src_solid
            acc[take] += src_rgb[take]
            weight[take] += 1
        fill = weight > 0
        if not fill.any():
            break
        rgb[fill] = (acc[fill] / weight[fill, None]).astype(np.uint8)
        solid = solid | fill


def _bust_crop(rgba: np.ndarray) -> np.ndarray:
    """Head through the shirt, dropping the legs so the face stays large."""
    rgb = rgba[:, :, :3].astype(np.int16)
    alpha = rgba[:, :, 3]
    opaque = alpha > 16
    if not opaque.any():
        raise RuntimeError("toon portrait rendered fully transparent")
    red = opaque & (rgb[:, :, 0] > 160) & (rgb[:, :, 1] < 120) & (rgb[:, :, 2] < 120)
    ys, xs = np.where(opaque)
    top = int(ys.min())
    if red.any():
        bottom = int(np.where(red)[0].max())
    else:
        bottom = int(ys.max())
    pad = 8
    y0 = max(0, top - pad)
    y1 = min(rgba.shape[0] - 1, bottom + pad)
    band = opaque[y0 : y1 + 1]
    xs = np.where(band.any(axis=0))[0]
    x0 = max(0, int(xs.min()) - pad)
    x1 = min(rgba.shape[1] - 1, int(xs.max()) + pad)
    return rgba[y0 : y1 + 1, x0 : x1 + 1].copy()


def render_portrait_rgba(resources: Path) -> np.ndarray:
    _install_otp_stub()
    builtins.choice = lambda cond, a, b: a if cond else b
    ott = Path(__file__).resolve().parents[2] / "ref" / "open-toontown"
    if not (ott / "toontown" / "toon" / "Toon.py").is_file():
        raise RuntimeError(f"missing upstream toon source at {ott} (git submodule update --init ref/open-toontown)")
    if str(ott) not in sys.path:
        sys.path.insert(0, str(ott))

    loadPrcFileData("", f"model-path {resources}")
    loadPrcFileData("", "default-model-extension .bam")
    loadPrcFileData("", "audio-library-name null")
    loadPrcFileData("", "load-display pandagl")
    loadPrcFileData("", "framebuffer-alpha true")
    loadPrcFileData("", "alpha-bits 8")
    loadPrcFileData("", "color-bits 8 8 8")
    loadPrcFileData("", "win-size 640 640")
    loadPrcFileData("", "show-frame-rate-meter 0")
    loadPrcFileData("", "notify-level error")

    from direct.showbase.ShowBase import ShowBase

    base = ShowBase(windowType="offscreen")
    base.setBackgroundColor(0, 0, 0, 0)
    base.win.setClearColor((0, 0, 0, 0))

    from toontown.toon.Toon import Toon
    from toontown.toon.ToonDNA import ToonDNA

    dna = ToonDNA()
    # Long dog, medium shorts, medium legs, male, light blue, white gloves, shirt 0.
    dna.newToonFromProperties("dll", "ms", "m", "m", HEAD_COLOR, 0, HEAD_COLOR, HEAD_COLOR, 0, 0, 0, 0, 0, 0)
    toon = Toon()
    # NametagGroup.getNametag3d is a client widget. The portrait has no chat.
    toon.initializeNametag3d = lambda: None
    toon.setDNA(dna)
    toon.useLOD(1000)
    toon.pose("neutral", 0)
    for pattern in ("**/drop_shadow*", "**/dropShadow*"):
        shadow = toon.find(pattern)
        if not shadow.isEmpty():
            shadow.hide()
    # generateToonColor paints **/head* and, for other species, **/ear?-*.
    # The new-anim dog mesh names that geom "ears", so the DNA head color
    # never reaches it and the ears stay black. Same setColor the head uses.
    toon.findAllMatches("**/ears*").setColor(dna.getHeadColor())
    toon.setPosHpr(*TOON_POS, TOON_H, 0, 0)
    toon.reparentTo(base.render)

    ambient = AmbientLight("banner-ambient")
    ambient.setColor(Vec4(0.95, 0.95, 0.95, 1))
    base.render.setLight(base.render.attachNewNode(ambient))
    key = DirectionalLight("banner-key")
    key.setColor(Vec4(0.45, 0.45, 0.45, 1))
    key_np = base.render.attachNewNode(key)
    key_np.setHpr(-30, -35, 0)
    base.render.setLight(key_np)

    base.camLens.setFov(MAT_CAM_FOV)
    base.camLens.setNear(0.1)
    base.camera.setPosHpr(*MAT_CAM_POS, *MAT_CAM_HPR)
    # Panda's camera looks along +Y. Dolly along that axis; do not retarget.
    forward = base.camera.getQuat().xform(Vec3(0, 1, 0))
    forward.normalize()
    head = toon.find("**/1000/**/__Actor_head")
    bounds = head.getTightBounds(base.render)
    if not bounds:
        raise RuntimeError("dog head has no geometry")
    lo, hi = bounds
    target = (lo + hi) * 0.5
    target.z = lo.z + (hi.z - lo.z) * 0.15
    head_h = hi.z - lo.z
    along = (target - base.camera.getPos()).dot(forward)
    # Head about half the frame, so the shirt still fits under the chin.
    distance = head_h / (0.50 * 2 * math.tan(math.radians(MAT_CAM_FOV) / 2))
    base.camera.setPos(base.camera.getPos() + forward * (along - distance))

    for _ in range(6):
        base.graphicsEngine.renderFrame()
    black = _screenshot(base.win)
    base.win.setClearColor((1, 1, 1, 0))
    for _ in range(3):
        base.graphicsEngine.renderFrame()
    white = _screenshot(base.win)

    rgba = _straighten_alpha(black, white)
    _bleed_rgb(rgba)
    cropped = _bust_crop(rgba)
    # Banner quads put the last row on +Y. PNM row 0 is the top of this shot
    # (the ears), so flip it to match the logo plane.
    return np.flipud(cropped)
