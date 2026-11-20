#!/usr/bin/env python3
"""view_grabs.py RUN_DIR OUT_DIR : the Azahar framebuffer grab for each autotest "view: <name>" step.

RUN_DIR is a views_run.sh / azahar_test.sh output dir with log.txt and grab/ (az_grab_wrapper.sh
grabs plus clock.txt). For each view the grab whose app-log clock is latest inside the step (at
least 1.5 s before the next step) is cropped to OUT_DIR/shot_view_<name>_top.png (400x240) and
_bottom.png, the names compare_views.py reads.
"""
import os
import re
import sys
from PIL import Image

run, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
steps = []
for line in open(f'{run}/log.txt', errors='ignore'):
    m = re.match(r'\[(\d+)\] autotest: (?:step \d+ \'(.*)\'|finished)', line)
    if m:
        steps.append((int(m.group(1)), m.group(2)))
clock = []
for line in open(f'{run}/grab/clock.txt'):
    p = line.split()
    if len(p) == 2 and os.path.exists(f'{run}/grab/{p[0]}.png'):
        clock.append((int(p[1]), p[0]))
for i, (t, label) in enumerate(steps):
    if not label or not label.startswith(('view: ', 'pose: ')):
        continue
    end = steps[i + 1][0] if i + 1 < len(steps) else 1 << 62
    inside = [(c, n) for c, n in clock if t + 3000 <= c <= end - 1500]
    view = label[6:] if label.startswith('view: ') else 'pose_' + label[6:]
    if not inside:
        print(f'{view}: no grab inside the step')
        continue
    c, n = inside[-1]
    im = Image.open(f'{run}/grab/{n}.png').convert('RGB')
    im.crop((440, 202, 840, 442)).save(f'{out}/shot_view_{view}_top.png')
    im.crop((480, 442, 800, 682)).save(f'{out}/shot_view_{view}_bottom.png')
    print(f'{view}: grab {n} (log {c - t} ms into the step)')
