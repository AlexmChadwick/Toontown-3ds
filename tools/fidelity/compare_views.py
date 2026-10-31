#!/usr/bin/env python3
"""Old-vs-New 3DS fidelity gate. No emulator execution.

compare_views.py OLD_DIR NEW_DIR --old-log L --new-log L
Reads shot_view_<view>_top.png (400x240). numpy + scipy + Pillow.
Missing images, malformed sizes and missing draw evidence fail closed.
The by-eye real-game comparison is a separate mandatory tester check.
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


def load(directory, view):
    with Image.open(Path(directory)/f'shot_view_{view}_top.png') as image:
        if image.size != (400, 240):
            raise ValueError(f'{view}: expected 400x240, got {image.size}')
        return np.asarray(image.convert('RGB')).astype(float)


def draws(log, view):
    result = None
    if log:
        for line in Path(log).read_text(errors='ignore').splitlines():
            if re.search(r'view(?:[ _:]|: )'+re.escape(view)+r'\b', line):
                match = re.search(r'draws[ =:]+(\d+)', line)
                if match:
                    result = int(match.group(1))
    return result


def compare(view, old, new, old_draws=None, new_draws=None):
    ol, nl = old.mean(2), new.mean(2)
    similarity = ssim_map(ol, nl)
    tiles = [similarity[y:y+48, x:x+50].mean()
             for y in range(0, 240, 48) for x in range(0, 400, 50)]
    big = (np.abs(old-new).mean(2) > 30).mean()*100
    why = []
    if similarity.mean() < .97:
        why.append(f'ssim {similarity.mean():.3f}<0.97')
    if min(tiles) < .85:
        why.append(f'tile ssim {min(tiles):.3f}<0.85 (tile {int(np.argmin(tiles))})')
    if big > 1:
        why.append(f'{big:.1f}% px differ >30 (max 1.0%)')
    if view in SKY_BOX:
        x0, y0, x1, y1 = SKY_BOX[view]
        for tag, img in (('old', ol), ('new', nl)):
            std = img[y0:y1, x0:x1].std()
            if std < 4:
                why.append(f'{tag} sky flat (top std {std:.1f}<4)')
    if view in DETAIL_R11:
        lo, ln = ndimage.laplace(ol).var(), ndimage.laplace(nl).var()
        for tag, lv in (('old', lo), ('new', ln)):
            if lv < 1.3*DETAIL_R11[view]:
                why.append(f'{tag} texture detail {lv:.0f}<1.3x r11 ({1.3*DETAIL_R11[view]:.0f})')
        if lo < .9*ln:
            why.append(f'old detail {lo:.0f} < 0.9 x new {ln:.0f}')
    if old_draws is None or new_draws is None:
        why.append('missing shot-time draws (both logs required)')
    elif old_draws < .9*new_draws:
        why.append(f'old draws {old_draws} < 0.9 x new {new_draws}')
    return why, similarity.mean(), min(tiles), big


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('--old-log')
    ap.add_argument('--new-log')
    args = ap.parse_args()
    fail = False
    for view in VIEWS:
        try:
            od, nd = draws(args.old_log, view), draws(args.new_log, view)
            why, sim, tile, diff = compare(view, load(args.old, view), load(args.new, view), od, nd)
            print(f"{'FAIL' if why else 'PASS'} {view}: ssim {sim:.3f} mintile {tile:.3f} "
                  f"diff {diff:.1f}% draws {od}/{nd}  {'; '.join(why)}")
            fail |= bool(why)
        except (OSError, ValueError) as error:
            print(f'FAIL {view}: {error}')
            fail = True
    return int(fail)


if __name__ == '__main__':
    raise SystemExit(main())
