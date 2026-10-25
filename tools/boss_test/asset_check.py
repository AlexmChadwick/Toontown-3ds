#!/usr/bin/env python3
"""Validate every emitted VP file, vertex order, budgets and reproducible hashes."""
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'sdmc-out/3ds/toontown3ds/data/boss'
prefix = 'ceo' if '--ceo' in sys.argv else 'cj' if '--cj' in sys.argv else 'cfo' if '--cfo' in sys.argv else 'vp'
report = json.loads((OUT / (prefix + '_manifest.json')).read_text())
for name in report['models']:
    assert name + '.ttm' in report['sha256'], f'Missing model in manifest: {name}'
for name in report['clips']:
    assert name in report['sha256'], f'Missing clip in manifest: {name}'
for name, expected in report['sha256'].items():
    assert hashlib.sha256((OUT / name).read_bytes()).hexdigest() == expected, name
for name, clip in report['clips'].items():
    blob = (OUT / name).read_bytes()
    header = struct.unpack_from('<4sHHIHHf3f3fIIIII', blob)
    magic, version, flags, vertices, frames, reserved, fps = header[:7]
    assert magic == b'TTA1' and version == 1 and flags <= 1 and not reserved
    assert vertices == clip['vertices'] and frames == clip['frames'] and fps == 12
    model = report['models'][name.split('_', 2)[0] + '_' + name.split('_', 2)[1]]
    assert vertices == model['vertices'], name
    assert header[14] == vertices * 8
    assert header[13] + frames * header[14] <= len(blob)
    if flags:
        assert header[15] + frames * 48 <= len(blob)
        matrices = struct.unpack_from('<' + 'f' * (frames * 12), blob, header[15])
        import math
        assert all(math.isfinite(value) for value in matrices)
# The previous faulty bake produced stationary tracks. Require independent frames.
for part in ('legs', 'torso', 'head'):
    blob = (OUT / f'{prefix}_{part}_Ff_speech.tta').read_bytes()
    header = struct.unpack_from('<4sHHIHHf3f3fIIIII', blob)
    offset, stride = header[13:15]
    assert len({blob[offset + frame * stride:offset + (frame + 1) * stride]
                for frame in range(header[4])}) > 1, part
anchor = (OUT / (prefix + '_lobby_anchor.txt')).read_text().split()
assert anchor[:2] == ['BPA1', '1'] and len(anchor) == 5
if prefix == 'vp':
    scene = (OUT / 'vp_scene.bin').read_bytes()
    assert len(scene) == 56 and struct.unpack_from('<4sHH', scene) == (b'BSC1', 1, 3)
if prefix == 'cj':
    scene = (OUT / 'cj_scene.bin').read_bytes()
    assert len(scene) == 56 and struct.unpack_from('<4sHH', scene) == (b'CJS1', 1, 4)
if prefix == 'ceo':
    scene = (OUT / 'ceo_scene.bin').read_bytes()
    assert len(scene) == 416 and struct.unpack_from('<4sHH', scene) == (b'BES1', 1, 13)
if prefix != 'ceo':
    npcs = (OUT / (prefix + '_npcs.bin')).read_bytes()
    magic, version, count = struct.unpack_from('<4sHH', npcs)
    assert magic == b'BND1' and version == 1 and count == (9 if prefix == 'cj' else 1 if prefix == 'cfo' else 41)
    assert len(npcs) == 8 + count * 19
    assert all(npcs[8 + row * 19 + 4] == ord('t') for row in range(count))
budget = report['budget']
assert budget['clip_pair_geometry'] <= 6 * 1024 * 1024
assert budget['textures'] <= 8 * 1024 * 1024
assert budget['triangles'] < 15000 and budget['batches'] < 150
print(json.dumps({'files': len(report['sha256']), 'clips': len(report['clips']),
                  'budget': budget, 'disk_bytes': report['disk_bytes']}, sort_keys=True))
print(prefix.upper() + ' assets: PASS')
