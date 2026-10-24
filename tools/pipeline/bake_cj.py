"""CJ extension of bake_boss.py. Invoked only with its --cj option."""
import ast
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True


def main(baker):
    from panda3d.core import NodePath, Vec3
    out = baker.OUT
    out.mkdir(parents=True, exist_ok=True)
    base = baker.ShowBase()
    resources = baker.RESOURCES
    report = {'version': 1, 'sample_fps': 12, 'max_texture': 128, 'models': {}, 'clips': {}}

    def model(name, node):
        _, report['models'][name] = baker.write_model(name + '.ttm', node)

    def load(path):
        node = base.loader.loadModel(str(resources / (path + '.bam')))
        if node.isEmpty():
            raise ValueError(f'Missing CJ asset {path}')
        return node

    scene = NodePath('cj_arena')
    courtroom = load('phase_11/models/lawbotHQ/LawbotCourtroom3')
    courtroom.setPos(0, 0, -71.601)
    courtroom.setScale(1)
    courtroom.reparentTo(scene)
    entrance = courtroom.find('**/elevator_origin')
    if entrance.isEmpty():
        raise ValueError('CJ courtroom elevator origin missing')
    entrance.getChildren().detach()
    entrance.setScale(1)
    load('phase_11/models/lawbotHQ/LB_Elevator').reparentTo(entrance)
    # Reflections duplicate the room and its collision; native rendering uses one scene.
    for pattern in ('Reflections', 'interiorrefl'):
        for node in courtroom.findAllMatches('**/' + pattern):
            node.stash()
    # Open the travel doors; retain their frames and static collision elsewhere.
    for pattern in ('SlidingDoor1', 'CR3_Door', 'Door_1'):
        for door in courtroom.findAllMatches('**/' + pattern):
            door.setZ(door.getZ() + 25)
    podium = courtroom.find('**/Podium')
    if not podium.isEmpty():
        podium.setZ(podium.getZ() - 19.145)
    jury = courtroom.find('**/JuryBox')
    if jury.isEmpty():
        raise ValueError('CJ jury box missing')
    holder = NodePath('jury_box')
    jury.wrtReparentTo(holder)
    model('cj_jury_box', holder)  # End pose; runtime subtracts moving offset.
    holder.removeNode()
    collision = baker.collide.extract(scene, fallback_floor='auto')
    collision.write(str(out / 'cj_arena.ttc'), max_bytes=1024 * 1024)
    model('cj_arena', scene)
    scene.removeNode()
    lobby = load('phase_11/models/lawbotHQ/LB_CH_Lobby')
    locator = lobby.find('**/elevator_locator')
    if locator.isEmpty():
        raise ValueError('CJ lobby elevator locator missing')
    anchor = locator.getPos(lobby)
    (out / 'cj_lobby_anchor.txt').write_text(f'BPA1 1\n{anchor.x:.6f} {anchor.y:.6f} {anchor.z:.6f}\n')
    report['lobby_anchor'] = list(anchor)
    lobby.removeNode()

    baker.bake_parts(base, report, 'cj', 'phase_11/models/char/lawbotBoss')
    scale = load('phase_11/models/lawbotHQ/scale')
    beam = scale.find('**/scaleBeam')
    defense = scale.find('**/defensePan')
    prosecution = scale.find('**/prosecutionPan')
    if any(node.isEmpty() for node in (beam, defense, prosecution)):
        raise ValueError('CJ scale moving parts missing')
    beam_pivot = scale.find('**/StandLocator1').getBounds().getCenter()
    defense_local = scale.find('**/DefenseLocator').getBounds().getCenter()
    prosecution_local = scale.find('**/ProsecutionLocator').getBounds().getCenter()
    for name, node in [('cj_scale_defense', defense), ('cj_scale_prosecution', prosecution)]:
        model(name, node)
        node.detachNode()
    model('cj_scale_beam', beam)
    beam.detachNode()
    for locator_node in scale.findAllMatches('**/*Locator*'):
        locator_node.hide()
    model('cj_scale_stand', scale)
    scale.removeNode()

    cannon = load('phase_4/models/minigames/toon_cannon')
    barrel = cannon.find('**/cannon')
    if barrel.isEmpty():
        raise ValueError('CJ cannon barrel missing')
    barrel_pivot = barrel.getPos(cannon)
    model('cj_cannon_barrel', barrel)
    barrel.detachNode()
    model('cj_cannon_base', cannon)
    cannon.removeNode()
    for name, path, size in [
        ('cj_chair', 'phase_11/models/lawbotHQ/JuryBoxChair', .5),
        ('cj_gavel', 'phase_11/models/lawbotHQ/LB_gavel', 1),
        ('cj_evidence', 'phase_3.5/models/props/tart', 1),
        ('cj_lawbook', 'phase_5/models/props/lawbook', 1),
    ]:
        node = load(path)
        holder = NodePath(name)
        node.reparentTo(holder)
        node.setScale(size)
        model(name, holder)
        holder.removeNode()
    # CJS1: version, count; barrel pivot, beam pivot, two pan centres (local).
    vectors = [barrel_pivot, beam_pivot, defense_local, prosecution_local]
    (out / 'cj_scene.bin').write_bytes(struct.pack('<4sHH12f', b'CJS1', 1, 4,
        *(float(component) for vector in vectors for component in vector)))
    report['scene_vectors'] = [list(vector) for vector in vectors]

    # Stock juror NPC DNA + the explicit witness DNA from DistributedLawbotBoss.
    sys.path.insert(0, str(baker.UPSTREAM))
    from toontown.toon import ToonDNA
    tree = ast.parse((baker.UPSTREAM / 'toontown/toon/NPCToons.py').read_text())
    dictionary = next(node.value for node in tree.body if isinstance(node, ast.Assign)
        and any(isinstance(target, ast.Name) and target.id == 'NPCToonDict' for target in node.targets))
    descriptions = {ast.literal_eval(key): value for key, value in zip(dictionary.keys, dictionary.values)}
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Tuple):
            for target in node.targets:
                if isinstance(target, ast.Subscript) and isinstance(target.value, ast.Name) and target.value.id == 'NPCToonDict':
                    descriptions[ast.literal_eval(target.slice)] = node.value
    records = []
    for npc_id in range(2001, 2009):
        description = descriptions[npc_id]
        properties = ast.literal_eval(description.elts[2])
        dna = ToonDNA.ToonDNA()
        if properties == 'r':
            dna.newToonRandom(npc_id, ast.literal_eval(description.elts[3]), 1)
        else:
            dna.newToonFromProperties(*properties)
        blob = dna.makeNetString()
        if isinstance(blob, str):
            blob = blob.encode('latin1')
        if len(blob) != 15:
            raise ValueError('Unexpected CJ juror DNA length')
        records.append(struct.pack('<I', npc_id) + blob)
    witness = b't\x1b\x00\x01\x01\x00\x03\x00\x03\x01\x10\x13\x00\x13\x13'
    records.append(struct.pack('<I', 0) + witness)
    (out / 'cj_npcs.bin').write_bytes(struct.pack('<4sHH', b'BND1', 1, len(records)) + b''.join(records))

    geometry = sum(model['geometry_bytes'] for model in report['models'].values())
    textures = sum(model['texture_bytes'] for model in report['models'].values())
    clips = {name.split('_', 2)[2][:-4] for name in report['clips']}
    largest = max(sum(report['clips'][f'cj_{part}_{clip}.tta']['frames'] *
        report['clips'][f'cj_{part}_{clip}.tta']['vertices'] * 8
        for part in ('legs', 'torso', 'head', 'treads')) for clip in clips)
    report['budget'] = {'model_geometry': geometry, 'textures': textures, 'max_clip': largest,
        'clip_pair_geometry': geometry + 2 * largest,
        'triangles': sum(model['triangles'] for model in report['models'].values()),
        'batches': sum(model['batches'] for model in report['models'].values())}
    if geometry + 2 * largest > 6 * 1024 * 1024 or textures > 8 * 1024 * 1024:
        raise ValueError('CJ combined model/clip budget exceeds Old 3DS limits')
    report['sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(out.glob('cj_*')) if path.suffix in ('.ttm', '.tta', '.ttc', '.bin', '.txt')}
    report['disk_bytes'] = sum((out / name).stat().st_size for name in report['sha256'])
    (out / 'cj_manifest.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(report, indent=2, sort_keys=True), flush=True)
