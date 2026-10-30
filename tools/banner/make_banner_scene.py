#!/usr/bin/env python3
"""Build a layered 3D HOME Menu banner scene from original Toontown assets."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
from panda3d.core import PNMImage, WindowProperties, loadPrcFileData

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from gltf_export import SceneObject, collect_meshes, make_textured_quad, write_glb  # noqa: E402
from scene_common import (  # noqa: E402
    CAMERA_FAR,
    CAMERA_FOV_DEG,
    CAMERA_NEAR,
    CAMERA_POS,
    LOGO_POS,
    Z_BACKDROP,
    build_backdrop,
    card_rotation_at,
    configure_panda,
    fit_logo_texture,
    prepare_dog_head,
)


def _bake_backdrop_quad(resources: Path) -> SceneObject:
    from direct.showbase.ShowBase import ShowBase

    from panda3d.core import ShaderAttrib

    loadPrcFileData("", "load-display pandagl")
    configure_panda(resources)

    class BakeApp(ShowBase):
        def __init__(self):
            super().__init__()
            self.win.requestProperties(WindowProperties.size(512, 256))
            build_backdrop(resources).reparentTo(self.render)
            self.render.setShaderOff(ShaderAttrib.M_off)
            self.render.setLightOff(1)
            lens = self.cam.node().getLens()
            lens.setFov(CAMERA_FOV_DEG)
            lens.setNear(CAMERA_NEAR)
            lens.setFar(CAMERA_FAR)
            self.camera.setPos(*CAMERA_POS)
            self.camera.lookAt(0, 0, 0)
            for _ in range(12):
                self.graphicsEngine.renderFrame()
            self._shot = PNMImage()
            self.win.getScreenshot(self._shot)

    app = BakeApp()
    shot = app._shot
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
    return SceneObject(
        mesh=make_textured_quad("ttc_backdrop", 32.0, 16.0, rgba),
        translation=(0.0, 0.0, Z_BACKDROP),
    )


def build_scene(resources: Path) -> list[SceneObject]:
    objects: list[SceneObject] = [_bake_backdrop_quad(resources)]
    objects[0] = SceneObject(
        mesh=objects[0].mesh,
        translation=(0.0, 0.0, Z_BACKDROP),
    )
    logo = fit_logo_texture(resources / "phase_3" / "maps")
    objects.append(
        SceneObject(
            mesh=make_textured_quad("logo", logo.width_units, logo.height_units, logo.rgba),
            translation=LOGO_POS,
            rotation_euler=card_rotation_at(LOGO_POS),
        )
    )
    head_root = prepare_dog_head(resources)
    for part in collect_meshes(head_root, "head"):
        objects.append(SceneObject(mesh=part))
    return objects


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", type=Path, default=ROOT / "ref" / "resources")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "build" / "banner")
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


if __name__ == "__main__":
    main()
