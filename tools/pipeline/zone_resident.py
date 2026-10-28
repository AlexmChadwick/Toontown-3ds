#!/usr/bin/env python3
"""Report the worst-case resident world memory of every baked zone (.tz) and fail on overrun.

    .venv/bin/python -I tools/pipeline/zone_resident.py [ZONE_DIR] [--top N]

For each chunk's PVS the all-far baseline (what Zone::update holds before any near upgrade) is
summed; geometry must stay under 6 MiB and textures under 8 MiB (AGENTS.md section 3), and the
far baseline under 15,000 triangles / 150 draws. Complements sd_budget.py (card size).
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import tz

ROOT = Path(__file__).resolve().parents[2]
KEYS = ('geometry_bytes', 'texture_bytes', 'batches', 'triangles')
CAPS = dict(zip(KEYS, (6 << 20, 8 << 20, 150, 15000)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('dir', nargs='?', type=Path, default=ROOT / 'sdmc-out/3ds/toontown3ds/data/zones')
    ap.add_argument('--top', type=int, default=8)
    args = ap.parse_args()
    rows, bad = [], 0
    for path in sorted(args.dir.glob('*.tz')):
        try:
            ch = tz.validate(path.read_bytes())['chunks']
        except ValueError as e:
            print(f'{path.name}: INVALID {e}')
            bad += 1
            continue
        worst = {k: max(sum(ch[j]['low_stats'][k] for j in c['vis']) for c in ch) for k in KEYS}
        for k in KEYS:
            if worst[k] > CAPS[k]:
                print(f'{path.name}: {k} {worst[k]} exceeds {CAPS[k]}')
                bad += 1
        rows.append((path.name, len(ch), worst))
    rows.sort(key=lambda r: -(r[2]['geometry_bytes'] + r[2]['texture_bytes']))
    print(f'{len(rows)} zones; worst far-baseline residency (KiB geom+tex, draws, tris):')
    for name, n, w in rows[:args.top]:
        print(f"  {name:10} {n:3} chunks {w['geometry_bytes']>>10:5} + {w['texture_bytes']>>10:5} KiB "
              f"{w['batches']:4} draws {w['triangles']:6} tris")
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
