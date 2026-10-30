#!/usr/bin/env python3
"""Build a layered 3D HOME Menu banner scene from original Toontown assets.

Exports a glTF binary for tools/banner/pycgfx (skyfloogle/pycgfx) and writes a flat
256x128 preview PNG (what the HOME Menu texture looks like from the banner camera).

Inputs (override with env vars or --resources):
  ref/resources/phase_3/maps/toontown-logo.jpg (+ _a.rgb)
  ref/resources/phase_3/models/char/tt_a_chr_dgm_shorts_head_500.bam  (default dog head)
  ref/resources/phase_4/models/props/icecream.bam

    python3 -I tools/banner/make_banner_scene.py [--out-dir build/banner]
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from panda3d.core import (
    CardMaker,
    FrameBufferProperties,
    GraphicsPipe,
    Loader,
    NodePath,
    PNMImage,
    Texture,
    WindowProperties,
    loadPrcFileData,
)
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from gltf_export import SceneObject, collect_meshes, make_textured_quad, write_glb  # noqa: E402

# HOME Menu banner camera (see pycgfx/banner-camera.gltf).
CAMERA_POS = (0.0, 1.0, 44.786)
CAMERA_FOV_DEG = 30.0


def _load_logo_rgba(maps_dir: Path) -> np.ndarray:
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
            rgba[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    return np.flipud(rgba)


def _sky_gradient(w: int, h: int) -> np.ndarray:
    top = np.array([0x66, 0xCC, 0xFF], dtype=np.float32)
    bot = np.array([0x33, 0x99, 0xEE], dtype=np.float32)
    arr = np.zeros((h, w, 4), dtype=np.uint8)
    for y in range(h):
        t = y / max(h - 1, 1)
        rgb = top * (1.0 - t) + bot * t
        arr[y, :, :3] = rgb.astype(np.uint8)
        arr[y, :, 3] = 255
    return arr


def _load_model(resources: Path, rel: str) -> NodePath:
    loadPrcFileData("", f"model-path {resources}")
    path = resources / rel
    node = Loader.get_global_ptr().loadSync(str(path))
    if node is None:
        raise SystemExit(f"failed to load {path}")
    return NodePath(node)


def build_scene(resources: Path) -> list[SceneObject]:
    maps = resources / "phase_3" / "maps"
    logo = _load_logo_rgba(maps)
    logo_h, logo_w = logo.shape[:2]
    aspect = logo_w / logo_h

    objects: list[SceneObject] = []

    # Backdrop card (behind everything).
    bg = _sky_gradient(256, 128)
    objects.append(
        SceneObject(
            mesh=make_textured_quad("backdrop", 26.0, 26.0 / 1.666, bg),
            translation=(0.0, 0.0, -4.5),
        )
    )

    # Logo on its own plane, slightly forward for parallax depth.
    logo_small = np.asarray(Image.fromarray(logo, "RGBA").resize((192, int(192 / aspect)), Image.Resampling.LANCZOS))
    logo_w_units = 14.0
    objects.append(
        SceneObject(
            mesh=make_textured_quad("logo", logo_w_units, logo_w_units / aspect, logo_small),
            translation=(0.0, 2.2, -1.2),
            scale=(1.0, 1.0, 1.0),
        )
    )

    head_root = _load_model(resources, "phase_3/models/char/tt_a_chr_dgm_shorts_head_250.bam")
    head_root.set_hpr(180, -8, 0)
    head_root.set_scale(3.2)
    head_root.set_pos(0.0, -1.5, -0.35)
    for part in collect_meshes(head_root, "head"):
        objects.append(SceneObject(mesh=part))

    return objects


def render_preview(resources: Path, out_png: Path) -> None:
    """Offscreen render of the banner scene for documentation / PR previews."""
    loadPrcFileData("", f"model-path {resources}")
    loadPrcFileData("", "window-type offscreen")
    loadPrcFileData("", "audio-library-name null")
    loadPrcFileData("", "load-display p3headlessgl")
    from direct.showbase.ShowBase import ShowBase

    maps = resources / "phase_3" / "maps"
    logo_rgba = _load_logo_rgba(maps)

    class PreviewApp(ShowBase):
        def __init__(self):
            super().__init__(windowType="offscreen")
            self.win.requestProperties(WindowProperties.size(512, 384))
            self.setBackgroundColor(0.4, 0.78, 1.0, 1.0)
            self.disableAll()
            lens = self.cam.node().getLens()
            lens.setFov(CAMERA_FOV_DEG)
            lens.setNear(26.5)
            lens.setFar(1000.0)
            self.camera.setPos(*CAMERA_POS)
            self.camera.lookAt(0, 0, 0)

            bg = _sky_gradient(256, 128)
            self._card_from_rgba(bg, 26.0, 26.0 / 1.666, (0, 0, -4.5))

            aspect = logo_rgba.shape[1] / logo_rgba.shape[0]
            self._card_from_rgba(logo_rgba, 14.0, 14.0 / aspect, (0, 2.2, -1.2))

            head = _load_model(resources, "phase_3/models/char/tt_a_chr_dgm_shorts_head_250.bam")
            head.reparentTo(self.render)
            head.set_hpr(180, -8, 0)
            head.set_scale(3.2)
            head.set_pos(0, -1.5, -0.35)

            for _ in range(3):
                self.taskMgr.step()

            tex = Texture()
            self.win.getScreenshot(tex)
            img = PNMImage()
            tex.store(img)
            small = PNMImage(256, 128)
            small.gaussianFilterFrom(1.0, img)
            out_png.parent.mkdir(parents=True, exist_ok=True)
            small.write(str(out_png))

        def _card_from_rgba(self, rgba: np.ndarray, w: float, h: float, pos: tuple[float, float, float]):
            tex = Texture()
            tex.setup2dTexture(rgba.shape[1], rgba.shape[0], Texture.T_unsigned_byte, Texture.F_rgba)
            tex.setRamImage(rgba[::-1].copy().tobytes())
            cm = CardMaker("card")
            cm.setFrame(-w * 0.5, w * 0.5, -h * 0.5, h * 0.5)
            card = self.render.attachNewNode(cm.generate())
            card.setTexture(tex)
            card.setPos(*pos)
            card.setTransparency(TransparencyAttrib.MAlpha)
            card.setTwoSided(True)

    from panda3d.core import TransparencyAttrib

    PreviewApp()


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", type=Path, default=ROOT / "ref" / "resources")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "build" / "banner")
    ap.add_argument("--skip-preview", action="store_true")
    args = ap.parse_args()
    resources = args.resources.resolve()
    if not resources.is_dir():
        raise SystemExit(f"missing resources dir: {resources} (git submodule ref/resources)")

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    objects = build_scene(resources)
    glb_path = out_dir / "banner_scene.glb"
    write_glb(objects, glb_path)
    print(f"wrote {glb_path} ({glb_path.stat().st_size} bytes, {len(objects)} nodes)")

    if not args.skip_preview:
        preview_path = out_dir / "banner_preview.png"
        try:
            render_preview(resources, preview_path)
            print(f"wrote {preview_path}")
        except Exception as exc:  # noqa: BLE001
            print(f"preview skipped ({exc})")


if __name__ == "__main__":
    main()
