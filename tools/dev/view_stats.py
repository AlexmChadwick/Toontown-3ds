#!/usr/bin/env python3
"""Summarise the autotest "view: <name>" steps of a log.txt (tools/dev/azahar_test.sh output).

    .venv/bin/python -I tools/dev/view_stats.py run/log.txt [label]

For each view, the last 3 per-second stats samples (the zone has streamed in by then) are
averaged: fps, main-thread busy ms, GPU ms, draws, triangles, texture binds, heap used,
linear and VRAM free. Prints one aligned table.
"""
import re
import sys

STATS = re.compile(r'stats: \[view: (\S+)\] fps ([\d.]+) cpu ([\d.]+) gpu ([\d.]+) cmdbuf (\d+)% skipped (\d+) '
                   r'draws (\d+) tris (\d+) uniforms (\d+) texbinds (\d+) vbo (\d+)')
MEM = re.compile(r'stats: \[view: (\S+)\] heap (\d+)K linfree (\d+)K vramfree (\d+)K')


def main():
    log = open(sys.argv[1], errors='replace').read().splitlines()
    title = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
    views, order = {}, []
    for line in log:
        m = STATS.search(line) or MEM.search(line)
        if not m:
            continue
        name = m.group(1)
        if name not in views:
            views[name] = {'s': [], 'm': []}
            order.append(name)
        views[name]['s' if m.re is STATS else 'm'].append([float(x) for x in m.groups()[1:]])
    cols = ('fps', 'cpu_ms', 'gpu_ms', 'cmdbuf%', 'draws', 'tris', 'texbinds', 'heapK', 'linfreeK', 'vramfreeK')
    print(f'# {title}')
    print(f'{"view":<12}' + ''.join(f'{c:>10}' for c in cols))
    for name in order:
        s, m = views[name]['s'][-3:], views[name]['m'][-3:]
        if not s or not m:
            continue
        avg = lambda rows, i: sum(r[i] for r in rows) / len(rows)
        row = [avg(s, 0), avg(s, 1), avg(s, 2), avg(s, 3), avg(s, 5), avg(s, 6), avg(s, 8), avg(m, 0), avg(m, 1), avg(m, 2)]
        print(f'{name:<12}' + ''.join(f'{v:>10.1f}' if i < 3 else f'{v:>10.0f}' for i, v in enumerate(row)))


if __name__ == '__main__':
    main()
