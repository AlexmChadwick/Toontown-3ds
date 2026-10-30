"""TTC-style backdrop and logo planes for the HOME Menu banner."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from panda3d.core import (
    Geom,
    GeomNode,
    GeomTriangles,
    GeomVertexData,
    GeomVertexFormat,
    GeomVertexWriter,
    NodePath,
    PNMImage,
    Texture,
    TransparencyAttrib,
    loadPrcFileData,
)

# HOME Menu banner camera (pycgfx/banner-camera.gltf).
CAMERA_POS = (0.0, 1.0, 44.786)
CAMERA_FOV_DEG = 30.0
CAMERA_NEAR = 26.5
CAMERA_FAR = 1000.0

Z_BACKDROP = -4.8
Z_LOGO = -0.35
Z_TOON = 1.85

# Logo ~60% of visible width at Z_LOGO; toon bust on the right, forward in Z.
# ~60% of the banner width at the logo depth (visible width ≈ 45 units).
LOGO_POS = (-7.6, 0.4, Z_LOGO)
# Waist-up bust on the right, bottom edge just above the hills (visible height ≈ 23).
TOON_POS = (13.2, 3.1, Z_TOON)
TOON_PLANE_H = 12.4

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


def configure_panda(resources: Path) -> None:
    loadPrcFileData("", f"model-path {resources}")
    loadPrcFileData("", "audio-library-name null")
    loadPrcFileData("", "texture-anisotropic-degree 0")


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


def fit_logo_texture(maps_dir: Path, *, tex_w: int = 512) -> LogoPlane:
    rgba = _load_logo_raw(maps_dir)
    mask = rgba[:, :, 3] > 20
    ys, xs = np.where(mask)
    pad = 8
    y0, y1 = max(0, ys.min() - pad), min(rgba.shape[0] - 1, ys.max() + pad + 10)
    x0, x1 = max(0, xs.min() - pad), min(rgba.shape[1] - 1, xs.max() + pad)
    crop = rgba[y0 : y1 + 1, x0 : x1 + 1].copy()
    aspect = crop.shape[1] / crop.shape[0]
    width_units = 28.0
    height_units = width_units / aspect
    th = tex_w // 2 if aspect >= 2 else int(tex_w / aspect)
    tw = tex_w if aspect >= 2 else int(tex_w * aspect)
    img = Image.fromarray(crop, "RGBA").resize((tw, th), Image.Resampling.LANCZOS)
    # Quad texcoord v=0 sits on +Y and Panda/glTF sample this crop upright
    # without an extra flip (measured: Disney script above the yellow wordmark).
    return LogoPlane(np.asarray(img), width_units, height_units)


def compose_backdrop_rgba(resources: Path, *, w: int = 512, h: int = 256) -> np.ndarray:
    """Single TTC-style plate: sky, soft clouds, green hills (no 3D props)."""
    sky = Image.new("RGB", (w, h), (120, 190, 255))
    for y in range(h):
        t = y / max(h - 1, 1)
        r = int(120 + (70 - 120) * t)
        g = int(190 + (150 - 190) * t)
        b = int(255 + (220 - 255) * t)
        for x in range(w):
            sky.putpixel((x, y), (r, g, b))

    base = sky.convert("RGBA")
    clouds_path = resources / "phase_4/maps/mg_slingshot_clouds.jpg"
    alpha_path = resources / "phase_4/maps/mg_slingshot_clouds_alpha.jpg"
    if clouds_path.is_file() and alpha_path.is_file():
        cimg = Image.open(clouds_path).convert("RGB")
        aimg = Image.open(alpha_path).convert("L")
        cimg.putalpha(aimg)
        # The cloud map's alpha never falls below 108, so each paste is a hard
        # rectangle. The rightmost one sat behind the toon (about x=170–220 on
        # the 256-wide preview) and read as a pale card. The others stay, over
        # the logo side of the sky.
        for scale, ox, oy, op in (
            (0.62, -30, 4, 255),
            (0.52, 90, 12, 230),
            (0.48, 210, 2, 220),
        ):
            layer = cimg.resize((int(cimg.width * scale), int(cimg.height * scale)), Image.Resampling.LANCZOS)
            layer = layer.filter(ImageFilter.GaussianBlur(0.6))
            if op < 255:
                layer.putalpha(layer.getchannel("A").point(lambda a: a * op // 255))
            tmp = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            tmp.paste(layer, (ox, oy), layer)
            base = Image.alpha_composite(base, tmp)

    hills = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw = ImageDraw.Draw(hills)
    horizon = int(h * 0.64)
    draw.rectangle([0, horizon, w, h], fill=(72, 168, 64, 255))
    draw.polygon(
        [(0, horizon + 8), (w * 0.35, horizon - 6), (w * 0.7, horizon + 10), (w, horizon + 4), (w, h), (0, h)],
        fill=(58, 145, 52, 255),
    )
    draw.polygon(
        [(0, horizon + 20), (w * 0.5, horizon + 2), (w, horizon + 18), (w, h), (0, h)],
        fill=(46, 128, 42, 255),
    )
    out = Image.alpha_composite(base, hills)
    # Row 0 is the sky. The banner camera's +Y is up and samples the high-Y edge
    # of the quad (texcoord v=0) from the bottom of a top-row-first image, so the
    # plate is stored bottom-up and both the preview and the glTF see the sky on top.
    return np.flipud(np.asarray(out))


def _make_textured_card(
    rgba: np.ndarray,
    w: float,
    h: float,
    name: str = "card",
    *,
    depth_write: bool = True,
    straight_alpha: bool = False,
) -> NodePath:
    """XY-plane quad matching gltf_export.make_textured_quad (glTF Y-up, camera looks -Z)."""
    tex = Texture()
    tex.setup2dTexture(rgba.shape[1], rgba.shape[0], Texture.T_unsigned_byte, Texture.F_rgba)
    if straight_alpha:
        # No mip chain: a mip of a mostly-transparent portrait averages in the
        # toon and tints the whole card. Clamp so the border is not repeated.
        tex.setMinfilter(Texture.FTLinear)
        tex.setMagfilter(Texture.FTLinear)
        tex.setWrapU(Texture.WMClamp)
        tex.setWrapV(Texture.WMClamp)
    # Panda stores ram images BGRA, bottom row first. The numpy arrays are RGBA, top row first.
    ordered = np.flipud(rgba)
    bgra = ordered[..., [2, 1, 0, 3]].copy()
    tex.setRamImage(bgra.tobytes())
    fmt = GeomVertexFormat.getV3n3t2()
    vdata = GeomVertexData(name, fmt, Geom.UHStatic)
    vdata.setNumRows(4)
    vw = GeomVertexWriter(vdata, "vertex")
    nw = GeomVertexWriter(vdata, "normal")
    tw = GeomVertexWriter(vdata, "texcoord")
    hw, hh = w * 0.5, h * 0.5
    for x, y, u, v in (
        (-hw, -hh, 0.0, 1.0),
        (hw, -hh, 1.0, 1.0),
        (hw, hh, 1.0, 0.0),
        (-hw, hh, 0.0, 0.0),
    ):
        vw.addData3(x, y, 0.0)
        nw.addData3(0.0, 0.0, 1.0)
        tw.addData2(u, v)
    tris = GeomTriangles(Geom.UHStatic)
    tris.addVertices(0, 1, 2)
    tris.addVertices(0, 2, 3)
    tris.closePrimitive()
    geom = Geom(vdata)
    geom.addPrimitive(tris)
    node = GeomNode(name)
    node.addGeom(geom)
    card = NodePath(node)
    card.setTexture(tex)
    card.setTransparency(TransparencyAttrib.MAlpha)
    card.setTwoSided(True)
    card.setLightOff(1)
    card.setColor(1, 1, 1, 1)
    card.setDepthWrite(depth_write)
    return card


def attach_banner_scene(parent: NodePath, resources: Path, *, toon_rgba: np.ndarray) -> LogoPlane:
    backdrop = compose_backdrop_rgba(resources)
    back = _make_textured_card(backdrop, 56.0, 32.0, "backdrop")
    back.setPos(0, 0, Z_BACKDROP)
    back.reparentTo(parent)

    logo = fit_logo_texture(resources / "phase_3/maps")
    lcard = _make_textured_card(logo.rgba, logo.width_units, logo.height_units, "logo")
    lcard.setPos(*LOGO_POS)
    lcard.reparentTo(parent)

    # Same top-row-first layout as the logo. An extra flip stood the face on its head.
    aspect = toon_rgba.shape[1] / max(toon_rgba.shape[0], 1)
    tw = TOON_PLANE_H * aspect
    tcard = _make_textured_card(
        toon_rgba, tw, TOON_PLANE_H, "toon", depth_write=False, straight_alpha=True
    )
    tcard.setPos(*TOON_POS)
    tcard.reparentTo(parent)
    return logo


def card_hpr_for_banner() -> tuple[float, float, float]:
    return (0.0, 0.0, 0.0)
