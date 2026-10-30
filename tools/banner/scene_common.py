"""Shared HOME Menu banner scene layout and asset setup."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image
from panda3d.core import Loader, NodePath, PNMImage, Texture, TextureStage, loadPrcFileData

# HOME Menu banner camera (pycgfx/banner-camera.gltf).
CAMERA_POS = (0.0, 1.0, 44.786)
CAMERA_FOV_DEG = 30.0
CAMERA_NEAR = 26.5
CAMERA_FAR = 1000.0
CAMERA_LOOK_AT = (0.0, 0.0, 0.0)


def face_banner_camera(np: NodePath) -> None:
    """Orient a +Z-facing CardMaker quad toward the HOME Menu camera."""
    np.lookAt(*CAMERA_LOOK_AT)
    np.setP(np.getP() + 90.0)

# Depth layering (camera looks toward -Z; larger Z = closer to camera).
Z_BACKDROP = -6.0
Z_LOGO = -1.0
Z_HEAD = 1.4

LOGO_POS = (-4.8, 0.0, -0.5)
HEAD_POS = (5.0, 0.0, 2.2)
HEAD_HPR = (180.0, -18.0, 0.0)
HEAD_SCALE = 4.8

# Default Make-a-Toon style dog colours (head / body indices from stock tables).
HEAD_COLOR_IDX = 17  # light blue
BODY_COLOR_IDX = 9  # orange shirt tint on muzzle-adjacent if needed

ALL_COLORS = (
    (1.0, 1.0, 1.0),
    (0.96875, 0.691406, 0.699219),
    (0.933594, 0.265625, 0.28125),
    (0.863281, 0.40625, 0.417969),
    (0.710938, 0.234375, 0.4375),
    (0.570312, 0.449219, 0.164062),
    (0.640625, 0.355469, 0.269531),
    (0.996094, 0.695312, 0.511719),
    (0.832031, 0.5, 0.296875),
    (0.992188, 0.480469, 0.167969),
    (0.996094, 0.898438, 0.320312),
    (0.996094, 0.957031, 0.597656),
    (0.855469, 0.933594, 0.492188),
    (0.550781, 0.824219, 0.324219),
    (0.242188, 0.742188, 0.515625),
    (0.304688, 0.96875, 0.402344),
    (0.433594, 0.90625, 0.835938),
    (0.347656, 0.820312, 0.953125),
    (0.191406, 0.5625, 0.773438),
    (0.558594, 0.589844, 0.875),
    (0.285156, 0.328125, 0.726562),
    (0.460938, 0.378906, 0.824219),
    (0.546875, 0.28125, 0.75),
    (0.726562, 0.472656, 0.859375),
    (0.898438, 0.617188, 0.90625),
    (0.7, 0.7, 0.8),
    (0.3, 0.3, 0.35),
)


@dataclass(frozen=True)
class LogoPlane:
    rgba: np.ndarray
    width_units: float
    height_units: float


def _load_logo_raw(maps_dir: Path) -> np.ndarray:
    img = PNMImage(str(maps_dir / "toontown-logo.jpg"))
    alpha = PNMImage(str(maps_dir / "toontown-logo_a.rgb"))
    img.addAlpha()
    for y in range(img.getYSize()):
        for x in range(img.getXSize()):
            img.setAlpha(x, y, alpha.getGray(x, y))
    w, h = img.getXSize(), img.getYSize()
    rgba = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            c = img.getXelA(x, y)
            rgba[y, x] = [
                round(c[0] * 255),
                round(c[1] * 255),
                round(c[2] * 255),
                round(c[3] * 255),
            ]
    return np.flipud(rgba)


def fit_logo_texture(maps_dir: Path, *, max_tex: int = 256) -> LogoPlane:
    """Crop to opaque bounds so the full Toontown Online lockup fits with margin."""
    rgba = _load_logo_raw(maps_dir)
    mask = rgba[:, :, 3] > 24
    ys, xs = np.where(mask)
    if len(xs) == 0:
        raise RuntimeError("logo has no opaque pixels")
    pad = 10
    y0, y1 = max(0, ys.min() - pad), min(rgba.shape[0] - 1, ys.max() + pad + 8)
    x0, x1 = max(0, xs.min() - pad), min(rgba.shape[1] - 1, xs.max() + pad)
    crop = rgba[y0 : y1 + 1, x0 : x1 + 1].copy()
    aspect = crop.shape[1] / crop.shape[0]
    # Fit inside banner frustum: ~9 units tall with top/bottom margin.
    height_units = 5.6
    width_units = height_units * aspect
    if width_units > 13.0:
        width_units = 13.0
        height_units = width_units / aspect
    img = Image.fromarray(crop, "RGBA")
    tw = min(max_tex, int(max_tex * aspect))
    th = max_tex if tw == max_tex else int(max_tex / aspect)
    img = img.resize((tw, th), Image.Resampling.LANCZOS)
    return LogoPlane(np.asarray(img), width_units, height_units)


def configure_panda(resources: Path) -> None:
    loadPrcFileData("", f"model-path {resources}")
    loadPrcFileData("", "audio-library-name null")
    loadPrcFileData("", "texture-anisotropic-degree 0")


def load_model(resources: Path, rel: str) -> NodePath:
    configure_panda(resources)
    path = resources / rel
    node = Loader.get_global_ptr().loadSync(str(path))
    if node is None:
        raise RuntimeError(f"failed to load {path}")
    return NodePath(node)


def _remove(np_root: NodePath, pattern: str) -> None:
    for child in np_root.findAllMatches(pattern):
        child.removeNode()


def prepare_dog_head(resources: Path) -> NodePath:
    """Dog M-shorts head, neutral pose, stock textures and tint colours."""
    from direct.actor.Actor import Actor

    configure_panda(resources)
    base = "phase_3/models/char/tt_a_chr_dgm_shorts_head_500"
    actor = Actor(
        {"head": f"{base}.bam"},
        {"head": {"neutral": "phase_3/models/char/tt_a_chr_dgm_shorts_head_neutral.bam"}},
    )
    actor.pose("neutral", 22, "head")
    head = actor.getPart("head")
    # dls head variant cleanup (matches bake_toon.fix_head for dls code on dgm model path).
    _remove(head, "**/*long*")
    _remove(head, "**/muzzle-long*")
    _remove(head, "**/head-long")
    _remove(head, "**/head-front-long")
    eyes = head.find("**/eyes*")
    if not eyes.isEmpty():
        eyes.setColorOff()

    hc = ALL_COLORS[HEAD_COLOR_IDX]
    for part in head.findAllMatches("**/+GeomNode"):
        name = part.getName().lower()
        if "eye" in name or "pupil" in name:
            continue
        if "muzzle" in name or "head" in name or "ear" in name or name == "":
            part.setColorScale(hc[0], hc[1], hc[2], 1.0)

    root = NodePath("head_root")
    head.reparentTo(root)
    root.setPos(*HEAD_POS)
    root.setHpr(*HEAD_HPR)
    root.setScale(HEAD_SCALE)
    return root


def build_backdrop(resources: Path) -> NodePath:
    """TTC-style stack: sky card, clouds, gazebo on playground-tinted ground card."""
    root = NodePath("backdrop")
    sky = _sky_gradient_rgba(512, 256)
    sky_card = _make_textured_card(sky, 36.0, 18.0)
    sky_card.reparentTo(root)
    sky_card.setPos(0, 0, Z_BACKDROP)

    try:
        clouds = load_model(resources, "phase_4/models/props/test_clouds.bam")
        clouds.reparentTo(root)
        clouds.setPos(-10, 2.5, Z_BACKDROP + 0.8)
        clouds.setScale(0.4)
        clouds.setHpr(0, 0, 0)
    except RuntimeError:
        pass

    try:
        gazebo = load_model(resources, "phase_4/models/modules/gazebo.bam")
        gazebo.reparentTo(root)
        gazebo.setPos(-1.5, -5.5, Z_BACKDROP + 0.25)
        gazebo.setScale(0.28)
        gazebo.setHpr(25, 0, 0)
    except RuntimeError:
        pass

    grass = _ttc_grass_rgba(512, 128)
    ground = _make_textured_card(grass, 34.0, 5.0)
    ground.reparentTo(root)
    ground.setPos(0, -5.2, Z_BACKDROP + 0.1)
    return root


def _make_textured_card(rgba: np.ndarray, w: float, h: float) -> NodePath:
    from panda3d.core import CardMaker, TransparencyAttrib

    tex = Texture()
    tex.setup2dTexture(rgba.shape[1], rgba.shape[0], Texture.T_unsigned_byte, Texture.F_rgba)
    tex.setRamImage(rgba[::-1].copy().tobytes())
    cm = CardMaker("card")
    cm.setFrame(-w * 0.5, w * 0.5, -h * 0.5, h * 0.5)
    card = NodePath(cm.generate())
    card.setTexture(tex)
    card.setTransparency(TransparencyAttrib.MAlpha)
    card.setTwoSided(True)
    return card


def _sky_gradient_rgba(w: int, h: int) -> np.ndarray:
    top = np.array([0.72, 0.88, 1.0])
    bot = np.array([0.45, 0.72, 0.98])
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        t = y / max(h - 1, 1)
        rgb = top * (1.0 - t) + bot * t
        arr[y, :, :3] = (rgb * 255).astype(np.uint8)
        arr[y, :, 3] = 255
    return arr


def _ttc_grass_rgba(w: int, h: int) -> np.ndarray:
    """Simple green ground strip using TTC playground palette tones."""
    top = np.array([0.45, 0.78, 0.35])
    bot = np.array([0.28, 0.62, 0.22])
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        t = y / max(h - 1, 1)
        rgb = top * (1.0 - t) + bot * t
        arr[y, :, :3] = (rgb * 255).astype(np.uint8)
        arr[y, :, 3] = 255
    return arr


def build_logo_card(logo: LogoPlane) -> NodePath:
    card = _make_textured_card(logo.rgba, logo.width_units, logo.height_units)
    card.setPos(*LOGO_POS)
    face_banner_camera(card)
    return card


def card_rotation_at(pos: tuple[float, float, float]) -> tuple[float, float, float]:
    tmp = NodePath("orient")
    tmp.setPos(*pos)
    face_banner_camera(tmp)
    return (tmp.getH(), tmp.getP(), tmp.getR())


def attach_full_scene(parent: NodePath, resources: Path) -> tuple[LogoPlane, NodePath, NodePath]:
    logo = fit_logo_texture(resources / "phase_3" / "maps")
    build_backdrop(resources).reparentTo(parent)
    build_logo_card(logo).reparentTo(parent)
    head = prepare_dog_head(resources)
    head.reparentTo(parent)
    return logo, parent, head
