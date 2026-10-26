"""Rebuild all racing outputs in fresh isolated processes and compare SHA-256."""
from pathlib import Path
import hashlib
import json
import subprocess
ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'sdmc-out/3ds/toontown3ds/data/racing'
OUT = ROOT / 'sdmc-out/racing2'
def hashes():
    return {str(p.relative_to(DATA)):hashlib.sha256(p.read_bytes()).hexdigest() for p in DATA.rglob('*') if p.suffix in ('.ttm','.ttc','.trc') and p.name not in ('kart_default.ttm','kart_pad.ttm')}
before = hashes()
commands = [['tools/pipeline/bake_racing.py'], *[['tools/pipeline/bake_racing_karts.py','--lod',str(lod)] for lod in range(3)], ['tools/pipeline/bake_racing_props.py'], ['tools/pipeline/gen_racing_tables.py']]
anchors = (ROOT / 'source/world/racing_tables.h').read_bytes()
for index, command in enumerate(commands):
    with (OUT / f'determinism-{index}.log').open('w') as log:
        subprocess.run([str(ROOT / '.venv/bin/python'),'-I','-B',*command],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    print('rebake', index + 1, '/', len(commands), 'complete',flush=True)
after = hashes()
assert before == after, [name for name in before if before[name] != after.get(name)]
assert anchors == (ROOT / 'source/world/racing_tables.h').read_bytes()
(OUT / 'determinism.json').write_text(json.dumps({'files':len(after),'byte_identical':True,'sha256':after},indent=2,sort_keys=True)+'\n')
print('PASS:',len(after),'racing binaries and generated anchor table byte-identical across fresh processes')
