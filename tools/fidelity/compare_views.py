#!/usr/bin/env python3
"""Old-vs-New 3DS fidelity gate. No emulator execution.

compare_views.py OLD_DIR NEW_DIR --old-log L --new-log L --stock DIR [--plan F]
Reads shot_view_<view>_top.png (400x240) and, for the near views, the stock render
stock_<view>.png that stock_detail.py writes (--out DIR). numpy + scipy + Pillow.
Missing images, malformed sizes and missing draw or stock evidence fail closed.
The by-eye real-game comparison is a separate mandatory tester check.
From Codex af25ec7 tools/fidelity/compare_views.py, with the QA-20261119-19 ruling of
2026-11-20: near texture detail needs >= 1.3x the stock render's score (same code, lens and
camera) and >= 1.0x r11; the Old >= 0.9x New draw check is waived for a view only when the
Old and New frames are byte-identical, and the waiver is printed. --plan takes view_plan.cpp's
dump to name the chunks Old keeps far that New draws near. Each column prints PASS or FAIL.
run_gate.sh in this directory drives it. Run with a python that has scipy (system python3).
"""
import argparse
from pathlib import Path
import re
import numpy as np
from PIL import Image
from scipy import ndimage

VIEWS = ['ttc_near', 'ttc_mid', 'ttc_far', 'ttc_tree', 'street_near', 'street_far', 'dd_near', 'dd_far']
SKY_BOX = {'ttc_far': (0, 0, 180, 30), 'ttc_tree': (110, 0, 260, 10),
           'street_far': (140, 0, 330, 12), 'dd_far': (30, 0, 400, 18)}
DETAIL_R11 = {'ttc_near': 604, 'street_near': 371, 'dd_near': 362}


def ssim_map(a, b):
    c1, c2 = (0.01*255)**2, (0.03*255)**2
    def f(x):
        return ndimage.gaussian_filter(x, 1.5)
    ma, mb = f(a), f(b)
    va, vb, cov = f(a*a)-ma*ma, f(b*b)-mb*mb, f(a*b)-ma*mb
    return ((2*ma*mb+c1)*(2*cov+c2))/((ma*ma+mb*mb+c1)*(va+vb+c2))


def load(directory, view, name=None):
    with Image.open(Path(directory)/(name or f'shot_view_{view}_top.png')) as image:
        if image.size != (400, 240):
            raise ValueError(f'{view}: expected 400x240, got {image.size}')
        return np.asarray(image.convert('RGB')).astype(float)


def far_only_in_old(plan, view):
    """Chunks view_plan.cpp's dump plans far (1) on Old and near (0) on New for a view."""
    lods = {}
    if plan:
        mode = None
        for line in Path(plan).read_text(errors='ignore').splitlines():
            head = re.match(r'(\S+) (old|new): draws', line)
            if head:
                mode = head.group(2) if head.group(1) == view else None
            elif mode and line.strip().startswith('plan cell'):
                lods[mode] = dict(re.findall(r'(\d+):(\d)/', line))
    old, new = lods.get('old', {}), lods.get('new', {})
    return [c for c in old if old[c] == '1' and new.get(c) == '0']


def draws(log, view):
    result = None
    if log:
        for line in Path(log).read_text(errors='ignore').splitlines():
            if re.search(r'view(?:[ _:]|: )'+re.escape(view)+r'\b', line):
                match = re.search(r'draws[ =:]+(\d+)', line)
                if match:
                    result = int(match.group(1))
    return result


def detail(grey):
    return ndimage.laplace(grey).var()


def compare(view, old, new, old_draws=None, new_draws=None, stock=None, far_chunks=None):
    """Returns (why, columns, ssim, min tile, % px differ). why lists the failures; columns maps
    each gate column to (passed, text); a waived column passes and says so."""
    ol, nl = old.mean(2), new.mean(2)
    similarity = ssim_map(ol, nl)
    tiles = [similarity[y:y+48, x:x+50].mean()
             for y in range(0, 240, 48) for x in range(0, 400, 50)]
    big = (np.abs(old-new).mean(2) > 30).mean()*100
    columns = {}
    columns['ssim'] = (similarity.mean() >= .97, f'{similarity.mean():.3f} (min 0.97)')
    columns['tile ssim'] = (min(tiles) >= .85, f'{min(tiles):.3f} (min 0.85, tile {int(np.argmin(tiles))})')
    columns['px differ'] = (big <= 1, f'{big:.1f}% >30 (max 1.0%)')
    if view in SKY_BOX:
        x0, y0, x1, y1 = SKY_BOX[view]
        stds = {tag: img[y0:y1, x0:x1].std() for tag, img in (('old', ol), ('new', nl))}
        columns['sky flat'] = (min(stds.values()) >= 4,
                          ' '.join(f'{t} std {v:.1f}' for t, v in stds.items()) + ' (min 4)')
    if view in DETAIL_R11:
        lo, ln = detail(ol), detail(nl)
        r11 = DETAIL_R11[view]
        if stock is None:
            columns['texture detail'] = (False, 'missing stock render (--stock required)')
        else:
            ls = detail(stock.mean(2))
            bar = max(1.3*ls, r11)
            columns['texture detail'] = (min(lo, ln) >= bar,
                                 f'old {lo:.0f} new {ln:.0f} vs 1.3x stock {1.3*ls:.0f} (stock {ls:.0f}), '
                                 f'1.0x r11 {r11}')
        columns['old/new detail'] = (lo >= .9*ln, f'old {lo:.0f} new {ln:.0f} (old >= 0.9x new)')
    if old_draws is None or new_draws is None:
        columns['draws'] = (False, 'missing shot-time draws (both logs required)')
    elif old_draws >= .9*new_draws:
        columns['draws'] = (True, f'{old_draws}/{new_draws} (old >= 0.9x new)')
    else:
        differing = int((old != new).any(2).sum())
        if differing == 0:
            far = f', Old far LOD chunks {"/".join(far_chunks)}' if far_chunks else ''
            columns['draws'] = (True, f'{old_draws}/{new_draws}: draw ratio waived: frames identical '
                                      f'(0 px differ){far}')
        else:
            columns['draws'] = (False, f'old draws {old_draws} < 0.9 x new {new_draws} '
                                       f'({differing} px differ, no waiver)')
    why = [f'{name} {text}' for name, (ok, text) in columns.items() if not ok]
    return why, columns, similarity.mean(), min(tiles), big


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('--old-log')
    ap.add_argument('--new-log')
    ap.add_argument('--stock', help="stock_detail.py --out dir (stock_<view>.png)")
    ap.add_argument('--plan', help="view_plan.cpp dump of every view, Old and New")
    args = ap.parse_args()
    fail = False
    for view in VIEWS:
        try:
            od, nd = draws(args.old_log, view), draws(args.new_log, view)
            stock = None
            if view in DETAIL_R11 and args.stock:
                stock = load(args.stock, view, f'stock_{view}.png')
            why, columns, sim, tile, diff = compare(view, load(args.old, view), load(args.new, view),
                                                    od, nd, stock, far_only_in_old(args.plan, view))
            print(f"{'FAIL' if why else 'PASS'} {view}: ssim {sim:.3f} mintile {tile:.3f} "
                  f"diff {diff:.1f}% draws {od}/{nd}  {'; '.join(why)}")
            for name, (ok, text) in columns.items():
                print(f"    {'PASS' if ok else 'FAIL'} {name}: {text}")
            fail |= bool(why)
        except (OSError, ValueError) as error:
            print(f'FAIL {view}: {error}')
            fail = True
    return int(fail)


if __name__ == '__main__':
    raise SystemExit(main())
