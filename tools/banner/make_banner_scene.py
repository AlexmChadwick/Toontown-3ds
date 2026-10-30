#!/usr/bin/env python3
"""Build the 3-plane HOME Menu banner glTF (backdrop + logo + toon portrait)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from gltf_export import SceneObject, make_textured_quad, write_glb  # noqa: E402
from scene_common import (  # noqa: E402
    LOGO_POS,
    TOON_PLANE_H,
    TOON_POS,
    Z_BACKDROP,
    Z_LOGO,
    Z_TOON,
    compose_backdrop_rgba,
    fit_logo_texture,
)


def build_scene(resources: Path, toon_rgba) -> list[SceneObject]:
    backdrop = compose_backdrop_rgba(resources)
    logo = fit_logo_texture(resources / "phase_3/maps")
    aspect = toon_rgba.shape[1] / toon_rgba.shape[0]
    toon_w = TOON_PLANE_H * aspect
    return [
        SceneObject(
            mesh=make_textured_quad("backdrop", 42.0, 24.0, backdrop),
            translation=(0.0, 0.0, Z_BACKDROP),
            scale=(-1.0, 1.0, 1.0),
        ),
        SceneObject(
            mesh=make_textured_quad("logo", logo.width_units, logo.height_units, logo.rgba),
            translation=LOGO_POS,
            scale=(-1.0, 1.0, 1.0),
        ),
        SceneObject(
            mesh=make_textured_quad("toon", toon_w, TOON_PLANE_H, toon_rgba),
            translation=TOON_POS,
            scale=(-1.0, 1.0, 1.0),
        ),
    ]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", type=Path, default=ROOT / "ref" / "resources")
    ap.add_argument("--out-dir", type=Path, default=ROOT / "build" / "banner")
    ap.add_argument("--toon-npy", type=Path, required=True)
    args = ap.parse_args()
    resources = args.resources.resolve()
    if not resources.is_dir():
        raise SystemExit(f"missing resources dir: {resources}")

    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    import numpy as np

    toon_rgba = np.load(args.toon_npy)
    objects = build_scene(resources, toon_rgba)
    glb_path = out_dir / "banner_scene.glb"
    write_glb(objects, glb_path)
    print(f"wrote {glb_path} ({glb_path.stat().st_size} bytes, {len(objects)} planes)")


if __name__ == "__main__":
    main()
