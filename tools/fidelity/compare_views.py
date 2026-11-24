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
- near_views.detail can be bought with sharpening halos, so QA-20261119-84 added three guards; a
  near view passes only with all four (each column printed with stock / old / new):
  (a) texture detail >= 1.3x stock and >= 1.0x r11 (masked Laplacian variance),
  (b) off-edge detail >= 1.0x stock (Laplacian variance >= 3 px from the strong edges of stock, Old
      and New),
  (c) high-pass amplitude (sigma 1.5) <= 1.2x stock,
  (d) mean edge overshoot <= 1.5x stock;
- the stock render counts only with a stock_meta.json whose lens is main.cpp kViewFovYDeg (55),
  whose poses equal main.cpp kViews and whose PNG hashes match the files (a wrong-lens, wrong-pose
  or swapped stock fails). The meta is written by the renderer, so it is also held to STOCK_PIN:
  TT_RESOURCES must be at the pinned commit (rechecked with git), the render scripts must hash to
  the pinned render_sha256, and the PNGs must be the pinned bytes (the render is byte-deterministic;
  a blurred stock re-hashed into its meta fails here). Changing the render means re-pinning here;
- QA-20261119-168: for ttc_near and dd_near only, the gag shop awning cell (x 280-320, y 0-40) is
  masked out of every frame for the 1.0x r11 floor (against r11's score under the same mask) and
  for (b); the 1.3x stock part of (a) stays unmasked. The gate prints the mask and the cell, and
  prints both masked and unmasked values;
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
import subprocess
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
# QA-20261119-168: the anti-strobe rule (AGENTS.md section 4) keeps the gag shop awning's edge-on
# sliver at its mean last mip, where stock and r11 alias it into a hatch. For these two views only,
# that cell (x0, y0, x1, y1) is masked out of every frame for the r11 floor and for (b). The 1.3x
# stock part of (a) stays unmasked. AWNING_R11 is r11's masked detail under that cell too
# (375.502334 / 212.312703, floored like DETAIL_R11).
AWNING_CELL = (280, 0, 320, 40)
AWNING_MASK_VIEWS = {'ttc_near', 'dd_near'}
AWNING_R11 = {'ttc_near': 375, 'dd_near': 212}
# The stock render (stock_detail.py, 400x240, 55 degree lens) is byte-identical run to run (r59 gate4 =
# r60 gate5); these are its inputs and outputs. Re-pin only after rendering from a reviewed tree.
STOCK_PIN = {
    'tt_resources_commit': 'd00df01af876cc9c5d1a22329f394a9a50245e9c',
    'render_sha256': 'ab9c41e3223032c2a9d87b238b835981b0d60c9ebfca1d068f02ffefcfe96be2',
    'png': {'ttc_near': '8e36898c2721c2bd165acedb25e88f30ccd49e978b8bdb0797a5a19026eef9ba',
            'street_near': 'c7b9cba8d39bd3d98587a19d854fd9dbdc7d57d762e09824dbbac71d79ee62d9',
            'dd_near': '0f8388874adb414c8a7d20bd6ba2de3c74a8c5c04473fb426d47cb4a4747b14c'},
}
# QA-20261119-84 near bar, besides (a) 1.3x stock / 1.0x r11.
OFF_EDGE_MIN = 1.0
HIGHPASS_MAX = 1.2
OVERSHOOT_MAX = 1.5
# ttc_near and dd_near stay unsharpened (QA-20261119-84 rejected the 200% mask) and miss the
# QA-19 near bar: (a) 1.3x stock and 1.0x r11, (b) off-edge 1.0x stock. QA-20261119-145: these
# KNOWN GAP floors are unratified (QA-84 set no floors, the cause is not isolated, see
# qa/known-gap-provenance.md), so they do not pass the view. Both views are scored on the full
# QA-19 / QA-84 bars like street_near and stay FAIL (gate red) until the bar is met or Alex rules
# on lowering it. The floors are only a regression check printed beside the result: (a) masked
# Laplacian detail, (b) off-edge ratio to stock, (d) overshoot ratio to stock, compared raw (QA
# ruling of 2026-11-23). They are the raw box gate values (ttc a 442.499893 b 0.617313 d 1.493757,
# dd a 257.717437 b 0.655692 d 1.110967, gates 1-3 on 53b7805) rounded the safe way: floors down,
# ceilings up.
KNOWN_GAP = {'ttc_near': {'a': 442, 'b': 0.61, 'd': 1.50},
             'dd_near': {'a': 257, 'b': 0.65, 'd': 1.12}}
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


def resources_commit(path):
    """git HEAD of a TT_RESOURCES checkout, or None."""
    try:
        out = subprocess.run(['git', '-C', str(path), 'rev-parse', 'HEAD'], capture_output=True, text=True)
    except OSError:
        return None
    return out.stdout.strip() or None


def check_stock(directory, main=near_views.MAIN, pin=None):
    """Problems with a stock_detail.py output dir (empty list = usable): stock_meta.json must
    exist, use main.cpp's near lens, match main.cpp kViews' zone / eye / target for every near view
    and hash each stock PNG it names; its resources commit (rechecked with git), render_sha256 (and
    the tree's render scripts) and PNG hashes must equal `pin` (STOCK_PIN)."""
    pin = STOCK_PIN if pin is None else pin
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
    if meta.get('tt_resources_commit') != pin['tt_resources_commit']:
        why.append(f"stock TT_RESOURCES commit {meta.get('tt_resources_commit')} != pinned {pin['tt_resources_commit']}")
    else:
        live = resources_commit(meta.get('tt_resources', ''))
        if live != pin['tt_resources_commit']:
            why.append(f"stock TT_RESOURCES {meta.get('tt_resources')} is at {live}, not the pinned commit")
    tree = near_views.render_sha256(Path(main).parents[1])
    if meta.get('render_sha256') != pin['render_sha256'] or tree != pin['render_sha256']:
        why.append(f"stock render scripts: meta {meta.get('render_sha256')}, tree {tree}, pinned {pin['render_sha256']}")
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
        digest = hashlib.sha256(png.read_bytes()).hexdigest() if png.is_file() else None
        if png.is_file() and digest != got.get('sha256'):
            why.append(f'{name} stock_{name}.png does not hash to stock_meta.json')
        if digest != pin['png'].get(name):
            why.append(f'{name} stock_{name}.png is not the pinned stock render')
    return why


def raw(value):
    """A measured value as the gate compares it (unrounded, 6 decimals)."""
    return f'{value:.6f}'


def rawx(value, stock):
    """A raw ratio to stock as the gate compares it."""
    return f'{value/stock:.6f}x' if stock else 'stock 0'


def compare(view, old, new, old_draws=None, new_draws=None, stock=None, far_chunks=None):
    """Returns (why, columns, ssim, min tile, % px differ). why lists the failures; columns maps
    each gate column to (passed, text); a waived column passes and says so."""
    ol, nl = old.mean(2), new.mean(2)
    similarity = ssim_map(ol, nl)
    tiles = [similarity[y:y+48, x:x+50].mean()
             for y in range(0, 240, 48) for x in range(0, 400, 50)]
    big = (np.abs(old-new).mean(2) > 30).mean()*100
    columns = {}
    columns['ssim'] = (similarity.mean() >= .97, f'{raw(similarity.mean())} (min 0.97)')
    columns['tile ssim'] = (min(tiles) >= .85, f'{raw(min(tiles))} (min 0.85, tile {int(np.argmin(tiles))})')
    columns['px differ'] = (big <= 1, f'{raw(big)}% >30 (max 1.0%)')
    if view in SKY_BOX:
        x0, y0, x1, y1 = SKY_BOX[view]
        stds = {tag: img[y0:y1, x0:x1].std() for tag, img in (('old', ol), ('new', nl))}
        columns['sky flat'] = (min(stds.values()) >= 4,
                          ' '.join(f'{t} std {raw(v)}' for t, v in stds.items()) + ' (min 4)')
    if view in DETAIL_R11:
        lo, ln = detail(ol), detail(nl)
        cell = AWNING_CELL if view in AWNING_MASK_VIEWS else None
        mo, mn = (near_views.detail(g, cell) for g in (ol, nl)) if cell else (lo, ln)
        r11 = AWNING_R11[view] if cell else DETAIL_R11[view]
        if cell:
            columns['awning mask'] = (True, f'awning cell x {cell[0]}-{cell[2]} y {cell[1]}-{cell[3]} masked '
                                            f'in every frame for the r11 floor and (b) (QA-20261119-168); '
                                            f'1.3x stock stays unmasked')
        bars = ('texture detail', 'off-edge detail', 'high-pass', 'overshoot')
        if stock is None:
            for name in bars:
                columns[name] = (False, 'missing or unverified stock render (see the stock line)')
        else:
            sl = stock.mean(2)
            ls = detail(sl)
            gap = KNOWN_GAP.get(view)
            text = (f'(a) masked stock {raw(ls)} old {raw(lo)} new {raw(ln)}: raw {raw(min(lo, ln))} '
                    f'({rawx(min(lo, ln), ls)} stock) vs 1.3x stock {raw(1.3*ls)}')
            if cell:
                text += (f'; awning-masked old {raw(mo)} new {raw(mn)}: raw {raw(min(mo, mn))} '
                         f'vs 1.0x r11 awning-masked {r11} (unmasked r11 {DETAIL_R11[view]})')
            else:
                text += f', 1.0x r11 {r11}'
            columns['texture detail'] = (min(lo, ln) >= 1.3*ls and min(mo, mn) >= r11, text)
            edges = near_views.edge_map(sl) | near_views.edge_map(ol) | near_views.edge_map(nl)
            es, eo, en = (near_views.off_edge(g, edges) for g in (sl, ol, nl))
            text = f'(b) 3 px off-edge stock {raw(es)} old {raw(eo)} new {raw(en)}: '
            if cell:
                bs, bo, bn = (near_views.off_edge(g, edges, cell=cell) for g in (sl, ol, nl))
                text += (f'unmasked raw {rawx(min(eo, en), es)}; awning-masked stock {raw(bs)} old {raw(bo)} '
                         f'new {raw(bn)}: raw {rawx(min(bo, bn), bs)} vs min {OFF_EDGE_MIN}x')
            else:
                bs, bo, bn = es, eo, en
                text += f'raw {rawx(min(eo, en), es)} vs min {OFF_EDGE_MIN}x'
            columns['off-edge detail'] = (min(bo, bn) >= OFF_EDGE_MIN*bs, text)
            hs, ho, hn = (near_views.highpass(g) for g in (sl, ol, nl))
            columns['high-pass'] = (max(ho, hn) <= HIGHPASS_MAX*hs,
                            f'(c) sigma 1.5 amplitude stock {raw(hs)} old {raw(ho)} new {raw(hn)}: '
                            f'raw {rawx(max(ho, hn), hs)} vs max {HIGHPASS_MAX}x')
            vs, vo, vn = (near_views.overshoot(g) for g in (sl, ol, nl))
            columns['overshoot'] = (max(vo, vn) <= OVERSHOOT_MAX*vs,
                            f'(d) mean edge overshoot stock {raw(vs)} old {raw(vo)} new {raw(vn)}: '
                            f'raw {rawx(max(vo, vn), vs)} vs max {OVERSHOOT_MAX}x')
            if gap:
                held = {'a': min(lo, ln) >= gap['a'],
                        'b': bool(es) and min(eo, en)/es >= gap['b'],
                        'd': bool(vs) and max(vo, vn)/vs <= gap['d']}
                mark = {k: 'held' if v else 'BROKEN' for k, v in held.items()}
                columns['known gap'] = (all(held.values()),
                                f'KNOWN GAP {view}, unratified floors, gate red vs QA-19: '
                                f'(a) raw {raw(min(lo, ln))} vs floor {gap["a"]} {mark["a"]}, '
                                f'(b) raw {rawx(min(eo, en), es)} vs floor {gap["b"]}x {mark["b"]}, '
                                f'(d) raw {rawx(max(vo, vn), vs)} vs ceiling {gap["d"]}x {mark["d"]}')
        columns['old/new detail'] = (lo >= .9*ln, f'old {raw(lo)} new {raw(ln)}: raw {rawx(lo, ln)} '
                                                   f'(old >= 0.9x new)')
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
                if name in ('known gap', 'awning mask'):
                    print(f"    {'' if ok else 'FAIL '}{text}")
                else:
                    print(f"    {'PASS' if ok else 'FAIL'} {name}: {text}")
            fail |= bool(why)
        except (OSError, ValueError) as error:
            print(f'FAIL {view}: {error}')
            fail = True
    return int(fail or bool(stock_problems))


if __name__ == '__main__':
    raise SystemExit(main())
