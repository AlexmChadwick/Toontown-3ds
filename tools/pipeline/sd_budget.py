#!/usr/bin/env python3
"""Report the SD data set size per directory and fail when a size budget is exceeded.

    .venv/bin/python -I tools/pipeline/sd_budget.py [DATA_DIR] [--top N] [--warn-only]

Budgets are ceilings set a few percent above the measured TZ v3 / pooled-interior set
(603 MB total, 2026-10-27), so a baker that goes back to copying atlas pages into every
chunk, or a new asset that doubles a directory, fails the bake instead of silently
growing the card image. Directories that are absent are skipped (partial bakes).
Raise a ceiling only together with the reason in docs/PROGRESS.md.
"""
import argparse
import sys
from pathlib import Path

sys.dont_write_bytecode = True

MB = 1 << 20
ROOT = Path(__file__).resolve().parents[2]
DEFAULT_DATA = ROOT / 'sdmc-out/3ds/toontown3ds/data'

TOTAL_MB = 650
# Per top-level directory ceilings (MB), only for directories whose size was measured after
# the v3/pooled rewrite (zones 131, level 58, interiors 42). The rest count toward the total.
DIR_MB = {'zones': 145, 'level': 65, 'interiors': 50}
# One chunked zone/level file is streamed piecewise but is also read as a whole by tools and
# copied by players; no single file may exceed this.
FILE_MB = {'.tz': 8, '.tlf': 8, '.ttp': 8}
DEFAULT_FILE_MB = 24


def tree(root):
    for path in sorted(root.rglob('*')):
        if path.is_file():
            yield path, path.stat().st_size


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('data', nargs='?', type=Path, default=DEFAULT_DATA)
    ap.add_argument('--top', type=int, default=8, help='largest files to list')
    ap.add_argument('--warn-only', action='store_true', help='report overruns but exit 0')
    args = ap.parse_args()
    data = args.data
    if not data.is_dir():
        print(f'sd_budget: {data} not found', file=sys.stderr)
        return 2

    dirs = {}
    files = []
    total = 0
    for path, size in tree(data):
        rel = path.relative_to(data)
        top = rel.parts[0] if len(rel.parts) > 1 else '.'
        dirs[top] = dirs.get(top, 0) + size
        files.append((size, rel))
        total += size

    problems = []
    print(f'SD data set {data}')
    for name in sorted(dirs):
        limit = DIR_MB.get(name)
        mb = dirs[name] / MB
        flag = ''
        if limit is None:
            print(f'  {name:<12} {mb:8.1f} MB')
            continue
        if mb > limit:
            flag = f'  OVER {limit} MB'
            problems.append(f'{name}/ is {mb:.1f} MB, ceiling {limit} MB')
        print(f'  {name:<12} {mb:8.1f} MB / {limit:>3}{flag}')
    flag = ''
    if total / MB > TOTAL_MB:
        flag = f'  OVER {TOTAL_MB} MB'
        problems.append(f'total is {total / MB:.1f} MB, ceiling {TOTAL_MB} MB')
    print(f'  {"total":<12} {total / MB:8.1f} MB / {TOTAL_MB}{flag}')

    for size, rel in files:
        limit = FILE_MB.get(rel.suffix, DEFAULT_FILE_MB)
        if size > limit * MB:
            problems.append(f'{rel} is {size / MB:.1f} MB, per-file ceiling {limit} MB')
    print(f'largest files:')
    for size, rel in sorted(files, reverse=True)[:args.top]:
        print(f'  {size / MB:7.2f} MB  {rel}')

    for p in problems:
        print(f'sd_budget: {p}', file=sys.stderr)
    if problems and not args.warn_only:
        return 1
    print('sd_budget: within budget' if not problems else 'sd_budget: over budget (warn only)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
