#!/usr/bin/env python3
"""Host schematic of the SpeedChat list at the EMOTIONS last-page scroll.

Writes docs/shots/gui-polish/speedchat_last_page.ppm (no third-party deps).
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs" / "shots" / "gui-polish"
OUT.mkdir(parents=True, exist_ok=True)

W, H = 320, 240
HEADER = 28
ROW_H = 35
ROW_INSET = 2
FRAME_BOTTOM = 170
MARGIN_X = 8
ROW_W = 320 - 2 * MARGIN_X - 8

num_rows = 24
whole_rows = 4
max_scroll = (num_rows - whole_rows) * ROW_H
list_bottom = HEADER + ROW_INSET + whole_rows * ROW_H
first_row = int(max_scroll / ROW_H)

pixels = bytearray(W * H * 3)

def fill_rect(x0, y0, x1, y1, rgb):
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            i = (y * W + x) * 3
            pixels[i : i + 3] = rgb

fill_rect(0, 0, W, H, (0x8b, 0xc8, 0xf0))
fill_rect(0, 208, W, H, (0x4a, 0x5a, 0x6a))
fill_rect(MARGIN_X, HEADER, MARGIN_X + ROW_W + 8, int(list_bottom), (0xff, 0xff, 0xff))

for i in range(whole_rows):
    row = first_row + i
    y = int(HEADER + ROW_INSET + row * ROW_H - max_scroll)
    if y + ROW_H > list_bottom + 0.5:
        raise SystemExit(f"row {row} overflows the frame ({y}+{ROW_H} > {list_bottom})")
    fill_rect(MARGIN_X + 4, y + 1, MARGIN_X + ROW_W - 4, y + ROW_H - 1, (0xf0, 0xf0, 0xf0))

bar_x = W - MARGIN_X - 4
fill_rect(bar_x, HEADER, bar_x + 4, int(list_bottom), (0xff, 0xff, 0xff))
fill_rect(bar_x, HEADER + 4, bar_x + 4, HEADER + 10, (0x22, 0x88, 0x22))

if max_scroll <= 0.5:
    raise SystemExit("expected EMOTIONS menu to scroll")
if abs(list_bottom - FRAME_BOTTOM) > 0.01:
    raise SystemExit(f"list bottom {list_bottom} != frame bottom {FRAME_BOTTOM}")

path = OUT / "speedchat_last_page.ppm"
with path.open("wb") as out:
    out.write(f"P6\n{W} {H}\n255\n".encode())
    out.write(pixels)
print(f"capture_speedchat_page: wrote {path.relative_to(ROOT)}")
