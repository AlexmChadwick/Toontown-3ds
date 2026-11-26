"""stats.py <gate dir>...: last fps / mem stats line per view in each Azahar log (qa/r4/stats-table.md shape)."""
import re, sys
VIEWS = 'ttc_near ttc_mid ttc_far ttc_tree street_near street_far dd_near dd_far'.split()
print('| gate | mode | view | fps | cpu ms | draws | tris | heap | linfree | vramfree |\n|---|---|---|---|---|---|---|---|---|---|')
for g in sys.argv[1:]:
    for mode in ('old', 'new'):
        last = {}
        for line in open(f'{g}/v{mode}/log.txt', errors='replace'):
            m = re.search(r'stats: \[view: (\w+)\] fps ([\d.]+) cpu ([\d.]+) .* draws (\d+) tris (\d+)', line)
            if m: last.setdefault(m[1], {}).update(fps=m[2], cpu=m[3], draws=m[4], tris=m[5])
            m = re.search(r'stats: \[view: (\w+)\] heap (\d+K) linfree (\d+K) vramfree (\d+K)', line)
            if m: last.setdefault(m[1], {}).update(heap=m[2], lin=m[3], vram=m[4])
        for v in VIEWS:
            s = last.get(v, {})
            print(f"| {g.rstrip('/').split('gate')[-1]} | {mode} | {v} | " + ' | '.join(s.get(k, '-') for k in ('fps', 'cpu', 'draws', 'tris', 'heap', 'lin', 'vram')) + ' |')
