"""Blank / void frame detector and sweep manifest for the offline facility and boss sweeps.

Each sweep frame is a 400x240 render of the stock scene and of our bake from the same camera,
over a background colour (BG) that no game texture uses, so a hole in the geometry is counted
exactly. A frame is judged against its stock render: an opening that is in the stock scene too
(a doorway out of the modelled room) is not a defect, but a hole, black slab or white slab that
only the bake has is. Stock frames that are themselves mostly background or one flat colour do
not show the room, so sweeps pick another camera spot instead of shooting them.

    python3 -I tools/fidelity/sweep_check.py <sweep-manifest.json>   # re-score, exit 1 on FAIL

The re-check re-scores each frame from the sheet PNGs next to the manifest (stats, judge), and
fails when a sheet is missing or its pixels no longer give the stored stats.
"""
import json
from pathlib import Path
import sys

import numpy as np

BG = (255, 0, 255)          # magenta: Panda background for every sweep render
BG_FLOAT = (1.0, 0.0, 1.0, 1.0)
VOID_MARGIN = .05           # bake may show 5% more background than stock
BLACK_MARGIN = .15
WHITE_MARGIN = .10
FLAT_MAX = .85              # one flat colour covering 85% of the frame
STOCK_VOID_MAX = .5         # a stock frame past these is not a view of the room
STOCK_FLAT_MAX = .75
# Level camera: at -8 the floor filled two thirds of an open yard's frame (the Sellbot factory
# sheet read as a grey void although stock and bake matched).
PITCH = 0


def erode(mask):
    """3x3 erosion: drops background specks and gaps under 3 px wide."""
    out = mask.copy()
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            out &= np.roll(np.roll(mask, dy, 0), dx, 1)
    return out


def stats(image):
    """Fractions of the frame that are background, near black, near white, and flat (within 8
    of the median colour on every channel, background excluded).

    'void' counts background only where it is at least 3 px across. Stock Panda blends M_dual
    foliage over the background, so the gaps between leaves come out tinted, while the bake
    alpha-tests them (AGENTS.md 4) and they come out exact background: the Bossbot hedges and
    trees read 5-7% more void with no geometry missing."""
    px = np.asarray(image.convert('RGB')).astype(np.int16)
    total = px.shape[0] * px.shape[1]
    bg = (np.abs(px - np.array(BG)).max(axis=2) <= 3)
    void = erode(bg)
    black = (px.max(axis=2) < 12) & ~bg
    white = (px.min(axis=2) > 243) & ~bg
    rest = px[~bg]
    flat = 0.0
    if len(rest):
        median = np.median(rest, axis=0)
        flat = float((np.abs(rest - median).max(axis=1) <= 8).sum()) / total
    return {'void': round(float(void.mean()), 4), 'black': round(float(black.sum()) / total, 4),
            'white': round(float(white.sum()) / total, 4), 'flat': round(flat, 4)}


def stock_ok(s):
    """A stock frame that actually shows the room (the camera spot is usable)."""
    return s['void'] <= STOCK_VOID_MAX and s['flat'] <= STOCK_FLAT_MAX and s['black'] <= .6


def judge(stock, baked):
    """Reasons a baked frame is broken next to its stock frame (empty list = pass)."""
    why = []
    if baked['void'] > stock['void'] + VOID_MARGIN:
        why.append(f"void {baked['void']:.2f} > stock {stock['void']:.2f}+{VOID_MARGIN}")
    if baked['black'] > stock['black'] + BLACK_MARGIN:
        why.append(f"black {baked['black']:.2f} > stock {stock['black']:.2f}+{BLACK_MARGIN}")
    if baked['white'] > stock['white'] + WHITE_MARGIN:
        why.append(f"white {baked['white']:.2f} > stock {stock['white']:.2f}+{WHITE_MARGIN}")
    if baked['flat'] > FLAT_MAX and baked['flat'] > stock['flat'] + .10:
        why.append(f"flat {baked['flat']:.2f} > {FLAT_MAX} (stock {stock['flat']:.2f})")
    return why


def frame_entry(sheet, row, eye, heading, columns):
    """columns: [(name, stats)], stock first. Returns the manifest entry for one sheet row."""
    stock = columns[0][1]
    entry = {'sheet': sheet, 'row': row, 'eye': [round(v, 2) for v in eye], 'heading': heading,
             'pitch': PITCH, 'stock_ok': stock_ok(stock), 'columns': {}}
    fails = []
    for name, s in columns:
        col = dict(s)
        if name != columns[0][0]:
            col['why'] = judge(stock, s)
            fails += [f'{name}: {w}' for w in col['why']]
        entry['columns'][name] = col
    if not entry['stock_ok']:
        fails.append('stock frame does not show the room')
    entry['result'] = 'FAIL' if fails else 'PASS'
    entry['why'] = fails
    return entry


def rescore(manifest, base):
    """Recomputes every frame entry from the saved sheet PNGs (row = 240 px, one 400 px column
    per manifest column), so the verdict comes from pixels, not from stored 'result' fields.
    Returns (frames by item name, mismatches between the stored and recomputed stats)."""
    from PIL import Image
    columns = manifest['columns']
    out, mismatch = {}, []
    for item in manifest['items']:
        frames = []
        path = Path(base) / item['sheet']
        if not path.is_file():
            mismatch.append(f"{item['name']}: sheet {item['sheet']} missing")
            out[item['name']] = frames
            continue
        with Image.open(path) as sheet:
            sheet = sheet.convert('RGB')
            for f in item['frames']:
                y = 240 * f['row']
                cols = [(name, stats(sheet.crop((400 * k, y, 400 * k + 400, y + 240))))
                        for k, name in enumerate(columns)]
                entry = frame_entry(item['sheet'], f['row'], f['eye'], f['heading'], cols)
                for name, _ in cols:
                    old = {k: v for k, v in f['columns'][name].items() if k != 'why'}
                    new = {k: v for k, v in entry['columns'][name].items() if k != 'why'}
                    if old != new:
                        mismatch.append(f"{item['name']} row {f['row']} {name}: stored {old} != frame {new}")
                frames.append(entry)
        out[item['name']] = frames
    return out, mismatch


def recheck(manifest, base):
    """Re-scores the frames in base (the manifest's directory). Exit status counts failing items,
    plus one for any stored stat the frames no longer reproduce."""
    frames, mismatch = rescore(manifest, base)
    bad = 0
    for item in manifest['items']:
        got = frames[item['name']]
        status = 'PASS' if got and all(f['result'] == 'PASS' for f in got) else 'FAIL'
        bad += status != 'PASS'
        print(f"{status} {item['name']}: {len(got)} frames re-scored from {item['sheet']}")
        for f in got:
            if f['result'] != 'PASS':
                print(f"    row {f['row']} eye {f['eye']} h {f['heading']}: {'; '.join(f['why'])}")
    for m in mismatch:
        print(f'MISMATCH {m}')
    return bad + bool(mismatch)


if __name__ == '__main__':
    total = 0
    for path in sys.argv[1:]:
        total += recheck(json.load(open(path)), Path(path).parent)
    sys.exit(1 if total else 0)
