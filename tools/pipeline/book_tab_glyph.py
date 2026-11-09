"""Sharpen the Shtiker Book Map tab (sos_textures teleportIcon).

ShtikerBook.addPageTab uses teleportIcon for the Map page: a red arrow into a dark
portal. bake_book_tabs.py then runs gaussianFilterFrom(1.0), which swells that arrow
into a blob and fills the portal solid. At the 21 px tab it reads as a dark well
with a red check, so the nine-icon collage looks like it stops at eight.

paint_teleport_icon redraws that same arrow, with a shaft, a downward head and a
portal rim, inside the Map cell. The baker calls it after the offscreen render so a
rebake does not put the blob back.
"""


def paint_teleport_icon(px, x, y, w, h):
    x0, y0 = int(x), int(y)
    x1, y1 = x0 + int(w), y0 + int(h)
    px[y0:y1, x0:x1] = 0
    cw, ch = int(w), int(h)
    rim = (32, 34, 40, 255)
    hole = (86, 94, 106, 255)
    red = (214, 36, 32, 255)
    cx, cy = cw * 0.50, ch * 0.70
    rx, ry = cw * 0.36, ch * 0.17
    for yy in range(ch):
        for xx in range(cw):
            nx = (xx + 0.5 - cx) / rx
            ny = (yy + 0.5 - cy) / ry
            d = nx * nx + ny * ny
            if d <= 1.0:
                px[y0 + yy, x0 + xx] = rim if d > 0.42 else hole
    mid = cw // 2
    shaft0, shaft1 = int(ch * 0.08), int(ch * 0.46)
    half = max(2, int(cw * 0.07))
    px[y0 + shaft0:y0 + shaft1, x0 + mid - half:x0 + mid + half + 1] = red
    base, tip = int(ch * 0.32), int(ch * 0.64)
    head = int(cw * 0.26)
    span = max(1, tip - base)
    for yy in range(base, tip):
        t = (yy - base) / span
        hw = max(1, int(round(head * (1.0 - t))))
        px[y0 + yy, x0 + mid - hw:x0 + mid + hw + 1] = red
