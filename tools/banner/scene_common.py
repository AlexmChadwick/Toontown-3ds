"""TTC-style backdrop and logo planes for the HOME Menu banner."""

from __future__ import annotations

import math
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
# Near the zero-parallax plane. The dog and the hills sit behind this.
Z_LOGO = -0.35
Z_TOON = -1.7

# Home Menu top screen is 400×240. banner-camera.gltf uses this 5:3 aspect,
# so a quad's world-unit aspect is its on-screen pixel aspect.
BANNER_ASPECT = 400.0 / 240.0
# phase_3 loading card is 1.83×1.0 in render2d. On a 4:3 window that is
# 1.83*(4/3) ≈ 2.44. toontown-logo.jpg is that wide art squeezed into a
# 256×256 square; the card un-squeezes it. Measuring the texture is 1:1.
LOGO_ASPECT = 1.83 * 4.0 / 3.0
# No documented pixel safe-area. Leave the top for HOME's title strip and
# the bottom for the camera and L/R hints.
TOP_BAND = 0.16
BOTTOM_BAND = 0.14
SIDE_MARGIN = 0.045
# Letterbox for the dog. Both sides are multiples of 8 (8×8 swizzle tiles).
# 128×160 RGBA8 is about 80 KiB, which fits beside a 256×256 RGBA8 logo
# under the 512 KiB decompressed CGFX cap.
TOON_TEX_W = 128
TOON_TEX_H = 160

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
    pos: tuple[float, float, float]


@dataclass(frozen=True)
class ToonPlane:
    rgba: np.ndarray
    width_units: float
    height_units: float
    pos: tuple[float, float, float]


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


def _visible_half_extents(z: float, aspect: float) -> tuple[float, float]:
    """Half-width and half-height of the perspective view at world z.

    The camera sits on +Z and looks down -Z. Vertical FOV matches
    banner-camera.gltf (30°). ``aspect`` is width/height of that view.
    """
    distance = CAMERA_POS[2] - z
    half_h = distance * math.tan(math.radians(CAMERA_FOV_DEG) * 0.5)
    return half_h * aspect, half_h


def _ndc_size(pixel_aspect: float, sy: float) -> float:
    """NDC width for a card ``sy`` NDC units tall. Full frame NDC is 2×2."""
    return sy * pixel_aspect / BANNER_ASPECT


def _logo_ndc() -> tuple[float, float, float, float]:
    """Upper-left card, just under the title strip. (x0, y0, x1, y1), y up."""
    sy = 0.82
    x0 = -1.0 + 2.0 * SIDE_MARGIN
    y1 = 1.0 - 2.0 * TOP_BAND - 0.02
    sx = _ndc_size(LOGO_ASPECT, sy)
    return (x0, y1 - sy, x0 + sx, y1)


def _toon_ndc(pixel_aspect: float) -> tuple[float, float, float, float]:
    """Lower-right card, above the hint strip and to the right of the logo."""
    sy = 1.02
    sx = _ndc_size(pixel_aspect, sy)
    x1 = 1.0 - 2.0 * SIDE_MARGIN
    y0 = -1.0 + 2.0 * BOTTOM_BAND
    return (x1 - sx, y0, x1, y0 + sy)


def _separated(a: tuple[float, float, float, float], b: tuple[float, float, float, float]) -> bool:
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b
    return ax1 <= bx0 or bx1 <= ax0 or ay1 <= by0 or by1 <= ay0


def assert_layout(toon_aspect: float) -> None:
    logo = _logo_ndc()
    toon = _toon_ndc(toon_aspect)
    if not _separated(logo, toon):
        raise SystemExit(f"logo NDC {logo} overlaps the dog {toon}")
    y_lo = -1.0 + 2.0 * BOTTOM_BAND - 1e-6
    y_hi = 1.0 - 2.0 * TOP_BAND + 1e-6
    for name, box in (("logo", logo), ("toon", toon)):
        x0, y0, x1, y1 = box
        if x0 < -1.0 or x1 > 1.0 or y0 < y_lo or y1 > y_hi:
            raise SystemExit(f"{name} leaves the safe frame: {box}")


def _world_card(
    z: float, ndc: tuple[float, float, float, float]
) -> tuple[float, float, tuple[float, float, float]]:
    x0, y0, x1, y1 = ndc
    half_w, half_h = _visible_half_extents(z, BANNER_ASPECT)
    width = (x1 - x0) * half_w
    height = (y1 - y0) * half_h
    pos = (
        (x0 + x1) * 0.5 * half_w,
        CAMERA_POS[1] + (y0 + y1) * 0.5 * half_h,
        z,
    )
    return width, height, pos


def fit_logo_texture(maps_dir: Path) -> LogoPlane:
    """Wide logo card. The texture stays 256×256; the quad un-squeezes it.

    UVs run 0..1 over the whole square, the same way ``loading-background.bam``
    shows this file. The card height is ``width / LOGO_ASPECT``.
    """
    rgba = _load_logo_raw(maps_dir)
    if rgba.shape[0] != 256 or rgba.shape[1] != 256:
        raise SystemExit(
            f"toontown-logo is {rgba.shape[1]}x{rgba.shape[0]}, expected 256x256"
        )
    width, height, pos = _world_card(Z_LOGO, _logo_ndc())
    aspect = width / height
    if abs(aspect - LOGO_ASPECT) > 1e-6:
        raise SystemExit(f"logo card aspect {aspect} != {LOGO_ASPECT}")
    return LogoPlane(rgba, width, height, pos)


def fit_toon_texture(rgba: np.ndarray) -> np.ndarray:
    """Uniform letterbox into 128×160. Does not stretch the portrait."""
    src_h, src_w = rgba.shape[0], rgba.shape[1]
    scale = min(TOON_TEX_W / src_w, TOON_TEX_H / src_h)
    tw = max(1, int(round(src_w * scale)))
    th = max(1, int(round(src_h * scale)))
    fitted = Image.fromarray(rgba, "RGBA").resize((tw, th), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", (TOON_TEX_W, TOON_TEX_H), (0, 0, 0, 0))
    canvas.paste(fitted, ((TOON_TEX_W - tw) // 2, (TOON_TEX_H - th) // 2))
    return np.asarray(canvas)


def place_toon(rgba: np.ndarray) -> ToonPlane:
    """Dog card behind the logo, lower right. Texture is the letterboxed portrait."""
    tex = fit_toon_texture(rgba)
    # The quad matches the texture, including its transparent padding, so the
    # portrait texels are not stretched. Padding is a pixel or two.
    aspect = tex.shape[1] / tex.shape[0]
    assert_layout(aspect)
    width, height, pos = _world_card(Z_TOON, _toon_ndc(aspect))
    return ToonPlane(tex, width, height, pos)


def _feathered_cloud(rgb: Image.Image, alpha: Image.Image) -> Image.Image:
    """Cloud RGBA whose border is transparent.

    ``mg_slingshot_clouds_alpha`` tracks luminance and never falls below 108,
    so pasting it raw leaves a hard rectangle. Remap that floor to 0, then
    multiply by a circular falloff so the paste has no straight side.
    """
    raw = np.asarray(alpha.convert("L"), dtype=np.float32)
    coverage = np.clip((raw - 108.0) / (255.0 - 108.0), 0.0, 1.0)
    height, width = coverage.shape
    yy = np.linspace(-1.0, 1.0, height, dtype=np.float32)[:, None]
    xx = np.linspace(-1.0, 1.0, width, dtype=np.float32)[None, :]
    # Circular falloff. A rectangular (L-infinity) fade still leaves straight
    # sides; distance from the center does not.
    radius = np.sqrt(xx * xx + yy * yy)
    t = np.clip((radius - 0.42) / (1.05 - 0.42), 0.0, 1.0)
    edge = 1.0 - (t * t * (3.0 - 2.0 * t))
    out = np.asarray(rgb.convert("RGB")).copy()
    rgba = np.dstack([out, (coverage * edge * 255.0).astype(np.uint8)])
    return Image.fromarray(rgba, "RGBA")


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
        cloud = _feathered_cloud(Image.open(clouds_path), Image.open(alpha_path))
        for scale, ox, oy, op in (
            (0.62, -30, 4, 255),
            (0.52, 90, 12, 230),
            (0.48, 210, 2, 220),
            (0.35, 340, 20, 200),
        ):
            layer = cloud.resize(
                (max(1, int(cloud.width * scale)), max(1, int(cloud.height * scale))),
                Image.Resampling.LANCZOS,
            )
            layer = layer.filter(ImageFilter.GaussianBlur(0.6))
            arr = np.asarray(layer).copy()
            # Lanczos ringing can put a hair of alpha back on the bitmap edge.
            arr[0, :, 3] = 0
            arr[-1, :, 3] = 0
            arr[:, 0, 3] = 0
            arr[:, -1, 3] = 0
            if op < 255:
                arr[:, :, 3] = (arr[:, :, 3].astype(np.uint16) * op // 255).astype(np.uint8)
            layer = Image.fromarray(arr, "RGBA")
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
    # pycgfx clamps each axis to 256 on its own, which would turn this 512×256
    # plate into a square. Downsample uniformly to 256×128 first.
    plate = out.resize((256, 128), Image.Resampling.LANCZOS)
    # Row 0 is the sky. The banner camera's +Y is up and samples the high-Y edge
    # of the quad (texcoord v=0) from the bottom of a top-row-first image, so the
    # plate is stored bottom-up and both the preview and the glTF see the sky on top.
    return np.flipud(np.asarray(plate))


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
    lcard.setPos(*logo.pos)
    lcard.reparentTo(parent)

    # Same top-row-first layout as the logo. An extra flip stood the face on its head.
    toon = place_toon(toon_rgba)
    tcard = _make_textured_card(
        toon.rgba,
        toon.width_units,
        toon.height_units,
        "toon",
        depth_write=False,
        straight_alpha=True,
    )
    tcard.setPos(*toon.pos)
    tcard.reparentTo(parent)
    return logo


def card_hpr_for_banner() -> tuple[float, float, float]:
    return (0.0, 0.0, 0.0)
