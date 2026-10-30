#!/usr/bin/env python3
"""Flat 256x128 banner preview when OpenGL is unavailable (CI / cloud agents)."""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from make_banner_scene import _load_logo_rgba, _sky_gradient  # noqa: E402


def main() -> None:
    resources = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "ref" / "resources"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "build" / "banner" / "banner_preview.png"
    maps = resources / "phase_3" / "maps"
    bg = Image.fromarray(_sky_gradient(256, 128), "RGBA")
    logo = Image.fromarray(_load_logo_rgba(maps), "RGBA")
    logo = logo.resize((200, int(200 * logo.height / logo.width)), Image.Resampling.LANCZOS)
    bg.paste(logo, ((256 - logo.width) // 2, 14), logo)
    out.parent.mkdir(parents=True, exist_ok=True)
    bg.save(out)
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
