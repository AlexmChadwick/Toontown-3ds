#!/usr/bin/env python3
"""Before/after shots of the autotest "view:" steps.

    .venv/bin/python -I tools/dev/compare_views.py BEFORE_DIR OUT_DIR LABEL=RUN_DIR [LABEL=RUN_DIR ...]

BEFORE_DIR holds <view>.png (top over bottom, 400x480). Each RUN_DIR is an azahar_test.sh output
dir (shot_view_<view>_{top,bottom}.png). Writes OUT_DIR/<label>/<view>.png in the same top-over-
bottom layout and OUT_DIR/compare/<view>.png: BEFORE and every run side by side, labelled.
"""
import os
import sys

from PIL import Image, ImageDraw

VIEWS = ['ttc_near', 'ttc_mid', 'ttc_far', 'ttc_tree', 'street_near', 'street_far', 'dd_near', 'dd_far']


def stacked(run, view):
    top = Image.open(f'{run}/shot_view_{view}_top.png').convert('RGB')
    bot = Image.open(f'{run}/shot_view_{view}_bottom.png').convert('RGB')
    im = Image.new('RGB', (400, 480))
    im.paste(top, (0, 0))
    im.paste(bot, ((400 - bot.width) // 2, 240))
    return im


def main():
    before, out = sys.argv[1], sys.argv[2]
    runs = [a.split('=', 1) for a in sys.argv[3:]]
    os.makedirs(f'{out}/compare', exist_ok=True)
    for view in VIEWS:
        cols = [('BEFORE', Image.open(f'{before}/{view}.png').convert('RGB'))]
        for label, run in runs:
            im = stacked(run, view)
            os.makedirs(f'{out}/{label}', exist_ok=True)
            im.save(f'{out}/{label}/{view}.png')
            cols.append((f'AFTER {label}', im))
        pad, gap = 18, 4
        sheet = Image.new('RGB', (len(cols) * (400 + gap) - gap, 480 + pad), 'white')
        d = ImageDraw.Draw(sheet)
        for i, (label, im) in enumerate(cols):
            x = i * (400 + gap)
            sheet.paste(im, (x, pad))
            d.text((x + 4, 3), f'{view}  {label}', fill='black')
        sheet.save(f'{out}/compare/{view}.png')
        print(f'{out}/compare/{view}.png')


if __name__ == '__main__':
    main()
