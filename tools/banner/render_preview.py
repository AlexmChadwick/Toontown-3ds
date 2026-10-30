#!/usr/bin/env python3
"""Render the banner scene preview (mono + stereo) with Panda3D + software GL."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from PIL import Image
from panda3d.core import (
    AmbientLight,
    DirectionalLight,
    PNMImage,
    ShaderAttrib,
    WindowProperties,
    loadPrcFileData,
)

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from scene_common import (  # noqa: E402
    CAMERA_FAR,
    CAMERA_FOV_DEG,
    CAMERA_NEAR,
    CAMERA_POS,
    attach_full_scene,
    configure_panda,
)


def _setup_display() -> None:
    loadPrcFileData("", "load-display pandagl")
    loadPrcFileData("", "show-frame-rate-meter 0")
    loadPrcFileData("", "sync-video 0")


def _setup_camera(base, eye_offset_x: float = 0.0) -> None:
    lens = base.cam.node().getLens()
    lens.setFov(CAMERA_FOV_DEG)
    lens.setNear(CAMERA_NEAR)
    lens.setFar(CAMERA_FAR)
    x, y, z = CAMERA_POS
    base.camera.setPos(x + eye_offset_x, y, z)
    base.camera.lookAt(0, 0, 0)


def _setup_lights(render) -> None:
    amb = AmbientLight("amb")
    amb.setColor((0.65, 0.65, 0.72, 1))
    render.setLight(render.attachNewNode(amb))
    sun = DirectionalLight("sun")
    sun.setColor((0.95, 0.92, 0.85, 1))
    sn = render.attachNewNode(sun)
    sn.setHpr(-35, -55, 0)
    render.setLight(sn)


def _screenshot(base, w: int, h: int) -> PNMImage:
    for _ in range(12):
        base.graphicsEngine.renderFrame()
    shot = PNMImage()
    base.win.getScreenshot(shot)
    out = PNMImage(w, h)
    out.gaussianFilterFrom(1.0, shot)
    return out


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


def render_previews(resources: Path, out_dir: Path) -> None:
    from direct.showbase.ShowBase import ShowBase

    configure_panda(resources)
    _setup_display()

    class BannerPreview(ShowBase):
        def __init__(self):
            super().__init__()
            self.win.requestProperties(WindowProperties.size(640, 320))
            self.setBackgroundColor(0.45, 0.72, 0.98, 1)
            attach_full_scene(self.render, resources)
            _setup_lights(self.render)

    base = BannerPreview()
    base.render.setShaderOff(ShaderAttrib.M_off)
    for gnode in base.render.findAllMatches("**/+GeomNode"):
        gnode.setLightOff(1)

    out_dir.mkdir(parents=True, exist_ok=True)

    _setup_camera(base, 0.0)
    mono = _screenshot(base, 256, 128)
    mono_path = out_dir / "banner_preview.png"
    mono.write(str(mono_path))

    iod = 0.55
    _setup_camera(base, -iod * 0.5)
    pil_left = _pnm_to_pil(_screenshot(base, 256, 128))
    _setup_camera(base, iod * 0.5)
    pil_right = _pnm_to_pil(_screenshot(base, 256, 128))

    stereo_path = out_dir / "banner_preview_stereo.png"
    stereo = Image.new("RGBA", (512, 128))
    stereo.paste(pil_left, (0, 0))
    stereo.paste(pil_right, (256, 0))
    stereo.save(stereo_path)

    anaglyph = Image.new("RGB", (256, 128))
    lr = np.array(pil_left.convert("RGB"), dtype=np.float32)
    rr = np.array(pil_right.convert("RGB"), dtype=np.float32)
    ag = np.zeros((128, 256, 3), dtype=np.uint8)
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
    args = ap.parse_args()
    render_previews(args.resources.resolve(), args.out_dir.resolve())


if __name__ == "__main__":
    main()
