"""Additive CEO asset bake; invoked by bake_boss.py --ceo."""
import hashlib
import json
import struct
import sys
sys.dont_write_bytecode = True


def main(baker):
    from panda3d.core import NodePath
    out = baker.OUT
    out.mkdir(parents=True, exist_ok=True)
    base = baker.ShowBase()
    report = {'version': 1, 'sample_fps': 12, 'max_texture': 128, 'models': {}, 'clips': {}}

    def load(path):
        node = base.loader.loadModel(str(baker.RESOURCES / (path + '.bam')))
        if node.isEmpty():
            raise ValueError(path)
        return node

    def model(name, node):
        _, report['models'][name] = baker.write_model(name + '.ttm', node)

    court = load('phase_12/models/bossbotHQ/BanquetInterior_1')
    entrance = court.find('**/elevator_origin')
    load('phase_12/models/bossbotHQ/BB_Inside_Elevator').reparentTo(entrance)
    # Leave the intermission passage open; the runtime owns phase gating.
    door = court.find('**/door3')
    if not door.isEmpty():
        door.setZ(door.getZ() + 30)
    tables = []
    for index in range(13):
        locator = court.find('**/TableLocator_%d' % (index + 1))
        if locator.isEmpty():
            raise ValueError('Missing CEO table locator')
        position = locator.getPos(court)
        tables.append([*position, locator.getH(court)])
    belts = []
    for index in range(2):
        start = court.find('**/conveyer_belt_start_%d' % (index + 1)).getPos(court)
        end = court.find('**/conveyer_belt_end_%d' % (index + 1)).getPos(court)
        belts.append([*start, *end])
        belt = load('phase_12/models/bossbotHQ/food_belt%d_model' % (index + 1))
        belt.reparentTo(court)
        belt.setPos(start)
    baker.collide.extract(court, fallback_floor='auto').write(str(out / 'ceo_arena.ttc'), max_bytes=1024*1024)
    model('ceo_arena', court)
    court.removeNode()
    table = load('phase_12/models/bossbotHQ/BanquetTableChairs')
    chairs = []
    for index in range(8):
        locator = table.find('**/chair_%d' % (index + 1))
        chairs.append([*locator.getPos(table), locator.getH(table)])
    pitcher = table.find('**/bottle_locator').getPos(table)
    model('ceo_table', table)
    table.removeNode()
    bottle = load('phase_12/models/bossbotHQ/tt_m_ara_bhq_seltzerBottle')
    nozzle = bottle.find('**/nozzle_tip').getPos(bottle)
    model('ceo_pitcher', bottle)
    bottle.removeNode()
    props = [('food', 'phase_12/models/bossbotHQ/canoffood'),
             ('ball', 'phase_6/models/golf/golf_ball'), ('putter', 'phase_6/models/golf/putter'),
             ('club', 'phase_12/models/char/bossbotBoss-golfclub'),
             ('gear', 'phase_9/models/char/gearProp')]
    for name, path in props:
        node = load(path)
        model('ceo_' + name, node)
        node.removeNode()
    # CEO head geoms reference small subsets of shared vertex arrays. Strip only
    # unreferenced vertices, using the same sorted index map on every posed frame.
    # Scope this extractor to CEO so VP/CFO/CJ byte streams stay unchanged.
    original_extractor = baker.StableBossExtractor
    class CompactCeoExtractor(original_extractor):
        def batches(self):
            import numpy as np
            batches = super().batches()
            for batch in batches:
                used = np.unique(batch.indices)
                batch.verts = batch.verts[used]
                batch.indices = np.searchsorted(used, batch.indices)
            return batches
    baker.StableBossExtractor = CompactCeoExtractor
    baker.bake_parts(base, report, 'ceo', 'phase_12/models/char/bossbotBoss')
    baker.StableBossExtractor = original_extractor
    # BES1: 13 table poses, 8 chair poses, 2 belt endpoint pairs, pitcher/nozzle.
    values = [v for rows in (tables, chairs, belts, [list(pitcher), list(nozzle)]) for row in rows for v in row]
    (out / 'ceo_scene.bin').write_bytes(struct.pack('<4sHH' + 'f'*len(values), b'BES1', 1, 13, *values))
    report['layout'] = {'tables': tables, 'chairs': chairs, 'belts': belts,
                        'pitcher': list(pitcher), 'nozzle': list(nozzle)}
    # The CEO elevator is in BossbotHQ's clubhouse. Resolve the same model the
    # pinned BossbotCogHQLoader uses; no guessed world-space anchor.
    import ast
    loader_source = (baker.UPSTREAM / 'toontown/coghq/BossbotCogHQLoader.py').read_text()
    tree = ast.parse(loader_source)
    paths = [node.value.value for node in ast.walk(tree) if isinstance(node, ast.Assign)
             and isinstance(node.value, ast.Constant) and any(isinstance(target, ast.Attribute)
             and target.attr == 'cogHQLobbyModelPath' for target in node.targets)]
    found = None
    for path in paths:
        if not (baker.RESOURCES / (path + '.bam')).exists():
            continue
        lobby = load(path)
        locator = lobby.find('**/elevator_locator')
        if not locator.isEmpty():
            found = list(locator.getPos(lobby))
        lobby.removeNode()
        if found is not None:
            break
    if found is None:
        raise ValueError('CEO lobby elevator locator missing')
    (out / 'ceo_lobby_anchor.txt').write_text('BPA1 1\n' + ' '.join(f'{v:.6f}' for v in found) + '\n')
    report['lobby_anchor'] = found
    geometry = sum(model['geometry_bytes'] for model in report['models'].values())
    textures = sum(model['texture_bytes'] for model in report['models'].values())
    clips = {name.split('_', 2)[2][:-4] for name in report['clips']}
    largest = max(sum(report['clips'][f'ceo_{part}_{clip}.tta']['frames'] *
                     report['clips'][f'ceo_{part}_{clip}.tta']['vertices'] * 8
                     for part in ('legs', 'torso', 'head', 'treads')) for clip in clips)
    report['budget'] = {'model_geometry': geometry, 'textures': textures, 'max_clip': largest,
                        'clip_pair_geometry': geometry + 2*largest,
                        'triangles': sum(model['triangles'] for model in report['models'].values()),
                        'batches': sum(model['batches'] for model in report['models'].values())}
    if geometry + 2*largest > 6*1024*1024 or textures > 8*1024*1024:
        raise ValueError('CEO asset budget exceeded: ' + str(report['budget']))
    report['sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in sorted(out.glob('ceo_*')) if path.suffix in ('.ttm','.tta','.ttc','.bin','.txt')}
    report['disk_bytes'] = sum((out / name).stat().st_size for name in report['sha256'])
    (out / 'ceo_manifest.json').write_text(json.dumps(report, indent=2, sort_keys=True)+'\n')
    print(json.dumps(report, indent=2, sort_keys=True), flush=True)
