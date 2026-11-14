#!/usr/bin/env python3
"""Bake the dog bust RGBA once for banner GLB + preview."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

from toon_portrait import render_portrait_rgba  # noqa: E402


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", type=Path, default=ROOT / "ref" / "resources")
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    rgba = render_portrait_rgba(args.resources.resolve())
    args.out.parent.mkdir(parents=True, exist_ok=True)
    np.save(args.out, rgba)
    print(f"wrote {args.out} shape={rgba.shape}")


if __name__ == "__main__":
    main()
