#!/usr/bin/env python3
"""Render the banner scene preview (mono + stereo) with Panda3D + software GL."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from panda3d.core import PNMImage, ShaderAttrib, loadPrcFileData

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from scene_common import (  # noqa: E402
    BANNER_ASPECT,
    CAMERA_FAR,
    CAMERA_FOV_DEG,
    CAMERA_NEAR,
    CAMERA_POS,
    attach_banner_scene,
    configure_panda,
)

# HOME Menu top screen. Scaling this 5:3 view into 256x128 would squash it.
PREVIEW_W = 400
PREVIEW_H = 240

def _setup_display() -> None:
    loadPrcFileData("", "load-display pandagl")
    loadPrcFileData("", "show-frame-rate-meter 0")
    loadPrcFileData("", "sync-video 0")
    # Set before ShowBase. requestProperties after init does not apply in time.
    loadPrcFileData("", "win-size 800 480")


def _setup_camera(base, eye_offset_x: float = 0.0) -> None:
    import math

    lens = base.cam.node().getLens()
    # pycgfx banner-camera.gltf: vertical FOV 30 degrees, aspect 400/240.
    aspect = BANNER_ASPECT
    vfov = CAMERA_FOV_DEG
    hfov = math.degrees(2.0 * math.atan(math.tan(math.radians(vfov) * 0.5) * aspect))
    lens.setFov(hfov, vfov)
    lens.setNear(CAMERA_NEAR)
    lens.setFar(CAMERA_FAR)
    x, y, z = CAMERA_POS
    base.camera.setPos(x + eye_offset_x, y, z)
    # XY cards face +Z. Panda's camera looks down -Y, so pitch -90 aims it along -Z
    # with +Y toward the top of the frame — the same view as the glTF banner camera.
    base.camera.setHpr(0, -90, 0)


def _screenshot_pil(base, w: int, h: int) -> Image.Image:
    for _ in range(16):
        base.graphicsEngine.renderFrame()
    shot = PNMImage()
    base.win.getScreenshot(shot)
    return _pnm_to_pil(shot).resize((w, h), Image.Resampling.LANCZOS)


def _pnm_to_pil(pnm: PNMImage) -> Image.Image:
    w, h = pnm.getXSize(), pnm.getYSize()
    rgba = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        for x in range(w):
            c = pnm.getXelA(x, y)
            rgba[y, x] = [
                round(c[0] * 255),
                round(c[1] * 255),
                round(c[2] * 255),
                round(c[3] * 255),
            ]
    return Image.fromarray(rgba, "RGBA")


def render_previews(resources: Path, out_dir: Path, toon_rgba: np.ndarray) -> None:
    from direct.showbase.ShowBase import ShowBase

    configure_panda(resources)
    _setup_display()

    class BannerPreview(ShowBase):
        def __init__(self):
            super().__init__()
            self.setBackgroundColor(0.47, 0.75, 1.0, 1)
            attach_banner_scene(self.render, resources, toon_rgba=toon_rgba)

    base = BannerPreview()
    win_w, win_h = base.win.getXSize(), base.win.getYSize()
    if abs(win_w / win_h - BANNER_ASPECT) > 0.02:
        raise SystemExit(f"preview window {win_w}x{win_h} is not 5:3")
    base.render.setShaderOff(ShaderAttrib.M_off)

    out_dir.mkdir(parents=True, exist_ok=True)

    _setup_camera(base, 0.0)
    mono_path = out_dir / "banner_preview.png"
    _screenshot_pil(base, PREVIEW_W, PREVIEW_H).save(mono_path)

    iod = 0.55
    _setup_camera(base, -iod * 0.5)
    pil_left = _screenshot_pil(base, PREVIEW_W, PREVIEW_H)
    _setup_camera(base, iod * 0.5)
    pil_right = _screenshot_pil(base, PREVIEW_W, PREVIEW_H)

    stereo_path = out_dir / "banner_preview_stereo.png"
    stereo = Image.new("RGBA", (PREVIEW_W * 2, PREVIEW_H))
    stereo.paste(pil_left, (0, 0))
    stereo.paste(pil_right, (PREVIEW_W, 0))
    stereo.save(stereo_path)

    lr = np.array(pil_left.convert("RGB"), dtype=np.float32)
    rr = np.array(pil_right.convert("RGB"), dtype=np.float32)
    ag = np.zeros((PREVIEW_H, PREVIEW_W, 3), dtype=np.uint8)
    ag[:, :, 0] = lr[:, :, 0].astype(np.uint8)
    ag[:, :, 1] = rr[:, :, 1].astype(np.uint8)
    ag[:, :, 2] = rr[:, :, 2].astype(np.uint8)
    anaglyph_path = out_dir / "banner_preview_anaglyph.png"
    Image.fromarray(ag, "RGB").save(anaglyph_path)

    print(f"wrote {mono_path}")
    print(f"wrote {stereo_path}")
    print(f"wrote {anaglyph_path}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", type=Path, default=ROOT / "ref" / "resources")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "build" / "banner")
    ap.add_argument("--toon-npy", type=Path, required=True)
    args = ap.parse_args()
    resources = args.resources.resolve()
    toon = np.load(args.toon_npy)
    render_previews(resources, args.out_dir.resolve(), toon)


if __name__ == "__main__":
    main()
