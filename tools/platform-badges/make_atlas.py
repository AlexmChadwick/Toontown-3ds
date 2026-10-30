#!/usr/bin/env python3
"""Platform badge kit: hand-written SVGs -> atlases, UV table and a preview sheet.

    python3 -P make_atlas.py            (writes next to this file)
    python3 -P make_atlas.py --out DIR

Needs cairosvg + Pillow (stock wheels, no network at run time). Deterministic: the
same SVGs give byte-identical PNG/JSON output.

Outputs
  atlas_64.png / atlas_32.png / atlas_16.png
      one row of 9 square cells; cell index == platform enum id; cell 0 (UNKNOWN) is
      left fully transparent. Premultiplied-safe: fully transparent pixels are RGB 0.
  atlas.json
      enum table plus, per size, pixel rects and normalised UV rects (v measured
      from the top row, i.e. image order; flip for bottom-up APIs).
  preview.png
      every badge at 64/32/16 on the nametag background, plus mock "Flippy"
      nametags with each badge left of the name at real nametag text height.
"""
import argparse
import io
import json
import os
import sys

import cairosvg
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

# The platform enum. Index == wire value of TTPlatformManager.setMyPlatform(uint8).
PLATFORMS = [
    (0, "UNKNOWN", None),
    (1, "WEB", "1_web.svg"),
    (2, "MAC", "2_mac.svg"),
    (3, "IOS", "3_ios.svg"),
    (4, "ANDROID", "4_android.svg"),
    (5, "WINDOWS", "5_windows.svg"),
    (6, "VR", "6_vr.svg"),
    (7, "N3DS", "7_n3ds.svg"),
    (8, "LINUX", "8_linux.svg"),
]
SIZES = (64, 32, 16)
SUPERSAMPLE = 8          # render at 8x, then a box/Lanczos reduction keeps 16 px crisp

# Toontown nametag look (CCNormal toon tag): grey-white card, toon-blue name.
TAG_BG = (204, 204, 204, 128)
TAG_FG = (77, 77, 179, 255)
SCENE = (122, 190, 92, 255)     # TTC lawn green behind the preview


def render_svg(path, size):
    png = cairosvg.svg2png(url=path, output_width=size * SUPERSAMPLE,
                           output_height=size * SUPERSAMPLE)
    big = Image.open(io.BytesIO(png)).convert("RGBA")
    # Reduce in premultiplied space so transparent edges do not pick up black fringes.
    return unpremultiply(premultiply(big).resize((size, size), Image.LANCZOS))


def premultiply(im):
    out = im.copy()
    px = out.load()
    w, h = out.size
    for y in range(h):
        for x in range(w):
            pr, pg, pb, pa = px[x, y]
            px[x, y] = (pr * pa // 255, pg * pa // 255, pb * pa // 255, pa)
    return out


def unpremultiply(im):
    px = im.load()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0:
                px[x, y] = (0, 0, 0, 0)
            else:
                px[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a), a)
    return im


def build(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    badges = {s: {} for s in SIZES}
    for pid, name, svg in PLATFORMS:
        if svg:
            for s in SIZES:
                badges[s][pid] = render_svg(os.path.join(HERE, "badges", svg), s)

    meta = {
        "version": 1,
        "note": "cell index == platform enum id; cell 0 (UNKNOWN) is empty. "
                "uv = [u0, v0, u1, v1] with v0 at the top row (image order).",
        "platforms": [{"id": pid, "name": name, "svg": svg and "badges/" + svg} for pid, name, svg in PLATFORMS],
        "atlases": {},
    }
    n = len(PLATFORMS)
    for s in SIZES:
        atlas = Image.new("RGBA", (s * n, s), (0, 0, 0, 0))
        rects = {}
        for pid, name, svg in PLATFORMS:
            if svg:
                atlas.paste(badges[s][pid], (pid * s, 0))
                rects[name] = {"id": pid, "px": [pid * s, 0, s, s],
                               "uv": [round(pid / n, 6), 0.0, round((pid + 1) / n, 6), 1.0]}
        fn = "atlas_%d.png" % s
        atlas.save(os.path.join(out_dir, fn), optimize=True)
        meta["atlases"][str(s)] = {"file": fn, "width": s * n, "height": s, "cell": s, "rects": rects}
    with open(os.path.join(out_dir, "atlas.json"), "w") as f:
        json.dump(meta, f, indent=2, sort_keys=False)
        f.write("\n")
    preview(badges, os.path.join(out_dir, "preview.png"))


def find_font():
    cands = [os.environ.get("TT_BADGE_FONT", ""),
             os.path.join(HERE, "..", "..", "ref", "resources", "phase_3", "models", "fonts", "ImpressBT.ttf"),
             os.path.join(HERE, "..", "pb", "Toontown-3ds", "ref", "resources", "phase_3", "models", "fonts", "ImpressBT.ttf"),
             "/workspace/toontown/claude/Toontown-3ds/ref/resources/phase_3/models/fonts/ImpressBT.ttf",
             "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return None


def nametag(font_path, badge, text, text_px):
    """A mock over-head nametag: rounded grey card, badge left of the name."""
    font = ImageFont.truetype(font_path, text_px) if font_path else ImageFont.load_default()
    d0 = ImageDraw.Draw(Image.new("RGBA", (1, 1)))
    l, t, r, b = d0.textbbox((0, 0), text, font=font)
    tw, th = r - l, b - t
    pad = max(3, text_px // 4)
    bs = badge.size[0] if badge else 0
    gap = pad // 2 if badge else 0
    w = pad * 2 + bs + gap + tw
    h = pad * 2 + max(th, bs)
    tag = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(tag)
    d.rounded_rectangle((0, 0, w - 1, h - 1), radius=h // 3, fill=TAG_BG)
    x = pad
    if badge:
        tag.alpha_composite(badge, (x, (h - bs) // 2))
        x += bs + gap
    d.text((x - l, (h - th) // 2 - t), text, font=font, fill=TAG_FG)
    return tag


def preview(badges, path):
    font = find_font()
    W, H = 1000, 520
    im = Image.new("RGBA", (W, H), SCENE)
    d = ImageDraw.Draw(im)
    label = ImageFont.truetype(font, 18) if font else ImageFont.load_default()
    names = [p for p in PLATFORMS if p[2]]
    # Rows of raw badges at each size on a nametag-coloured strip.
    y = 16
    for s in SIZES:
        strip = Image.new("RGBA", (W - 32, s + 16), (0, 0, 0, 0))
        ImageDraw.Draw(strip).rounded_rectangle((0, 0, W - 33, s + 15), radius=8, fill=TAG_BG)
        im.alpha_composite(strip, (16, y))
        for i, (pid, name, _) in enumerate(names):
            x = 32 + i * 118
            im.alpha_composite(badges[s][pid], (x, y + 8))
            if s == 64:
                d.text((x, y + s + 18), name, font=label, fill=(36, 21, 9, 255))
        y += s + 16 + (30 if s == 64 else 12)
    # Mock nametags at two real tag sizes (3DS-ish 16 px badge / desktop 32 px badge).
    y += 6
    for i, (pid, name, _) in enumerate(names):
        col, row = i % 4, i // 4
        tag = nametag(font, badges[32][pid], "Flippy", 30)
        im.alpha_composite(tag, (24 + col * 240, y + row * 56))
    y += 2 * 56 + 8
    for i, (pid, name, _) in enumerate(names):
        tag = nametag(font, badges[16][pid], "Flippy", 15)
        im.alpha_composite(tag, (24 + i * 118, y))
    y += 34
    tag = nametag(font, None, "Flippy (same platform: no badge)", 15)
    im.alpha_composite(tag, (24, y))
    # The 16 px cells blown up 4x (nearest) to judge pixel-level readability.
    y += 34
    d.text((24, y + 22), "16px x4", font=label, fill=(36, 21, 9, 255))
    for i, (pid, name, _) in enumerate(names):
        cell = Image.new("RGBA", (16, 16), TAG_BG[:3] + (255,))
        cell.alpha_composite(badges[16][pid])
        im.alpha_composite(cell.resize((64, 64), Image.NEAREST), (120 + i * 108, y))
    im.convert("RGB").save(path, optimize=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=HERE)
    a = ap.parse_args()
    build(a.out)
    print("wrote atlases %s, atlas.json, preview.png to %s" % (", ".join(map(str, SIZES)), a.out))


if __name__ == "__main__":
    sys.exit(main())
