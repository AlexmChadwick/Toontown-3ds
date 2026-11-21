#!/usr/bin/env python3
"""Old-vs-New 3DS fidelity gate. No emulator execution.

compare_views.py OLD_DIR NEW_DIR --old-log L --new-log L --stock DIR [--plan F]
Reads shot_view_<view>_top.png (400x240) and, for the near views, the stock render
stock_<view>.png plus stock_meta.json that stock_detail.py writes (--out DIR). numpy + scipy + Pillow.
Missing images, malformed sizes and missing draw or stock evidence fail closed.
The by-eye real-game comparison is a separate mandatory tester check.
From Codex af25ec7 tools/fidelity/compare_views.py, with the QA-20261119-19 ruling of
2026-11-20 and QA-20261119-74:
- near texture detail (near_views.detail: stock and 3DS frames both scored without the toon and
  nametag column x 165-245) needs >= 1.3x the stock render's and >= 1.0x r11's;
- the stock render counts only with a stock_meta.json whose lens is main.cpp kViewFovYDeg (55),
  whose poses equal main.cpp kViews and whose PNG hashes match the files (a wrong-lens, wrong-pose
  or swapped stock fails);
- the Old >= 0.9x New draw check is waived only for street_near, only when the Old and New frames
  are byte-identical, and only with --plan naming the chunks Old keeps far; the waiver is printed
  with those chunks.
Each column prints PASS or FAIL. run_gate.sh in this directory drives it. Run with a python that
has scipy (system python3).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import numpy as np
from PIL import Image
from scipy import ndimage
import near_views

VIEWS = ['ttc_near', 'ttc_mid', 'ttc_far', 'ttc_tree', 'street_near', 'street_far', 'dd_near', 'dd_far']
SKY_BOX = {'ttc_far': (0, 0, 180, 30), 'ttc_tree': (110, 0, 260, 10),
           'street_far': (140, 0, 330, 12), 'dd_far': (30, 0, 400, 18)}
# r11 (/workspace/shots/3ds-integ-r11/r11_views_new) under near_views.detail's mask; unmasked these
# frames score the ruling's 604 / 371 / 362.
DETAIL_R11 = {'ttc_near': 491, 'street_near': 167, 'dd_near': 281}
# Old draws fewer than New here because Old's lodNear (160) and 6 MiB texture retain keep chunks
# 6 and 54 at their far mesh; the QA ruling waives only this view, only for identical frames.
DRAW_WAIVER_VIEWS = {'street_near'}


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
    return near_views.detail(grey)


def check_stock(directory, main=near_views.MAIN):
    """Problems with a stock_detail.py output dir (empty list = usable): stock_meta.json must
    exist, use main.cpp's near lens, match main.cpp kViews' zone / eye / target for every near view
    and hash each stock PNG it names."""
    path = Path(directory) / 'stock_meta.json'
    if not path.is_file():
        return [f'no stock_meta.json in {directory} (rerun stock_detail.py)']
    try:
        meta = json.loads(path.read_text())
    except ValueError as error:
        return [f'stock_meta.json unreadable: {error}']
    why = []
    lens = near_views.fovy(main)
    if meta.get('fovy') != lens:
        why.append(f"stock lens {meta.get('fovy')} != main.cpp kViewFovYDeg {lens}")
    if meta.get('size') != [400, 240]:
        why.append(f"stock size {meta.get('size')} != [400, 240]")
    for name, view in near_views.views(main).items():
        if not view['r11_lens']:
            continue
        got = meta.get('views', {}).get(name)
        if got is None:
            why.append(f'{name} missing from stock_meta.json')
            continue
        for key in ('zone', 'eye', 'target'):
            if got.get(key) != view[key]:
                why.append(f'{name} stock {key} {got.get(key)} != main.cpp kViews {view[key]}')
        png = Path(directory) / f'stock_{name}.png'
        if png.is_file() and hashlib.sha256(png.read_bytes()).hexdigest() != got.get('sha256'):
            why.append(f'{name} stock_{name}.png does not hash to stock_meta.json')
    return why


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
            columns['texture detail'] = (False, 'missing or unverified stock render (see the stock line)')
        else:
            ls = detail(stock.mean(2))
            bar = max(1.3*ls, r11)
            columns['texture detail'] = (min(lo, ln) >= bar,
                                 f'masked old {lo:.0f} new {ln:.0f} vs 1.3x stock {1.3*ls:.0f} (stock {ls:.0f}), '
                                 f'1.0x r11 {r11}')
        columns['old/new detail'] = (lo >= .9*ln, f'old {lo:.0f} new {ln:.0f} (old >= 0.9x new)')
    if old_draws is None or new_draws is None:
        columns['draws'] = (False, 'missing shot-time draws (both logs required)')
    elif old_draws >= .9*new_draws:
        columns['draws'] = (True, f'{old_draws}/{new_draws} (old >= 0.9x new)')
    else:
        differing = int((old != new).any(2).sum())
        no = None
        if view not in DRAW_WAIVER_VIEWS:
            no = f'no waiver for {view}'
        elif differing:
            no = f'{differing} px differ, no waiver'
        elif not far_chunks:
            no = 'frames identical but no --plan chunks Old keeps far, no waiver'
        if no:
            columns['draws'] = (False, f'old draws {old_draws} < 0.9 x new {new_draws} ({no})')
        else:
            columns['draws'] = (True, f'{old_draws}/{new_draws}: draw ratio waived: frames identical '
                                      f'(0 px differ), Old far LOD chunks {"/".join(far_chunks)}')
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
    stock_problems = check_stock(args.stock) if args.stock else ['--stock required']
    print(f"{'FAIL' if stock_problems else 'PASS'} stock: {args.stock}: "
          f"{'; '.join(stock_problems) or 'stock_meta.json matches main.cpp lens and kViews, PNG hashes match'}")
    for view in VIEWS:
        try:
            od, nd = draws(args.old_log, view), draws(args.new_log, view)
            stock = None
            if view in DETAIL_R11 and not stock_problems:
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
    return int(fail or bool(stock_problems))


if __name__ == '__main__':
    raise SystemExit(main())
