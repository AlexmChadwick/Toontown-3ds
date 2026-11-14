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
    LOGO_ASPECT,
    Z_BACKDROP,
    compose_backdrop_rgba,
    fit_logo_texture,
    place_toon,
)


def build_scene(resources: Path, toon_rgba) -> list[SceneObject]:
    backdrop = compose_backdrop_rgba(resources)
    logo = fit_logo_texture(resources / "phase_3/maps")
    toon = place_toon(toon_rgba)
    # The logo node's transform is baked into the vertices and the node stays
    # at the identity. pycgfx has no billboard export; cgfx_convert sets Y-axial
    # billboard on this bone. A rotation on the node would fight that.
    print(
        f"logo card {logo.width_units:.4f} x {logo.height_units:.4f} "
        f"aspect {logo.width_units / logo.height_units:.4f} (target {LOGO_ASPECT:.4f}) "
        f"at {tuple(round(v, 3) for v in logo.pos)}"
    )
    print(
        f"toon card {toon.width_units:.4f} x {toon.height_units:.4f} "
        f"tex {toon.rgba.shape[1]}x{toon.rgba.shape[0]} "
        f"at {tuple(round(v, 3) for v in toon.pos)}"
    )
    return [
        SceneObject(
            mesh=make_textured_quad("backdrop", 56.0, 32.0, backdrop),
            translation=(0.0, 0.0, Z_BACKDROP),
        ),
        SceneObject(
            mesh=make_textured_quad(
                "name",
                logo.width_units,
                logo.height_units,
                logo.rgba,
                origin=logo.pos,
            ),
            translation=(0.0, 0.0, 0.0),
        ),
        SceneObject(
            mesh=make_textured_quad(
                "toon", toon.width_units, toon.height_units, toon.rgba
            ),
            translation=toon.pos,
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
