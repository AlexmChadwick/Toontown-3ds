#!/usr/bin/env python3
"""Bake VP or --cfo arenas and attached BossCog parts. Run with Python -I -B.

Assets are read through this checkout's ref/ref link. Output is restricted to its
ignored sdmc-out tree. All textures are at most 128 px, ETC1/ETC1A4 by default.
"""
import ast
import hashlib
import json
import math
import os
import re
import struct
from pathlib import Path
import sys

sys.dont_write_bytecode = True
os.environ['TT_ETC1_THREADS'] = '3'
os.environ['OPENBLAS_NUM_THREADS'] = '1'
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
# ref/ in the main checkout; agent worktrees link it as ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').exists() else ROOT / 'ref/ref'
RESOURCES = REF / 'resources'
UPSTREAM = REF / 'open-toontown'
OUT = ROOT / 'sdmc-out/3ds/toontown3ds/data/boss'
sys.path.insert(0, str(HERE))

import numpy as np
from panda3d.core import BamCache, Filename, GeomVertexData, GeomVertexReader, GeomVertexWriter, Mat4, NodePath, Thread, Vec3, loadPrcFileData
BamCache.getGlobalPtr().setActive(False)
loadPrcFileData('', 'window-type none\naudio-library-name null\nmodel-cache-dir\n'
                f'model-path {RESOURCES}\ntextures-power-2 none\nnotify-level error')
from direct.showbase.ShowBase import ShowBase
from direct.actor.Actor import Actor
import collide
import extract
import tta
import ttm


class StableBossExtractor(extract.Extractor):
    """Evaluate weighted skin matrices in double precision in a stable order.

    Boss torso vertices have multiple influences. Panda's float accumulation
    order varies with transform addresses, moving a few vertices across a TTA
    quantization boundary. Keep stock joint matrices, but sum their weights with
    math.fsum and transform the original vertices in float64.
    """
    validated = set()

    def _add_geom(self, geom, state, matrix, tint, layer, animated):
        source = geom.getVertexData()
        blends = source.getTransformBlendTable() if animated else None
        if blends is not None:
            matrices = []
            for index in range(blends.getNumBlends()):
                blend = blends.getBlend(index)
                terms = []
                for influence in range(blend.getNumTransforms()):
                    transform = Mat4()
                    blend.getTransform(influence).getMatrix(transform)
                    weight = blend.getWeight(influence)
                    terms.append([float(transform.getCell(row, column)) * weight
                                  for row in range(4) for column in range(4)])
                matrices.append([math.fsum(term[cell] for term in terms) for cell in range(16)]
                                if terms else np.eye(4).reshape(-1).tolist())
            matrices = np.array(matrices, dtype=np.float64).reshape(-1, 4, 4)
            rows = source.getNumRows()
            vertices = np.ones((rows, 4), dtype=np.float64)
            indices = np.zeros(rows, dtype=np.int32)
            positions = GeomVertexReader(source, 'vertex')
            weights = GeomVertexReader(source, 'transform_blend')
            for row in range(rows):
                vertices[row, :3] = positions.getData3()
                indices[row] = weights.getData1i()
            result = np.einsum('ni,nij->nj', vertices, matrices[indices])[:, :3]
            key = int(source.this)
            if key not in self.validated:
                panda = source.animateVertices(True, Thread.getCurrentThread())
                reader = GeomVertexReader(panda, 'vertex')
                expected = np.array([tuple(reader.getData3()) for _ in range(rows)])
                if np.max(np.abs(result - expected)) > 0.001:
                    raise ValueError('Stable boss skinning differs from Panda by more than 0.001 units')
                self.validated.add(key)
            data = GeomVertexData(source)
            writer = GeomVertexWriter(data, 'vertex')
            for position in result:
                writer.setData3(*position)
            geom = geom.makeCopy()
            geom.setVertexData(data)
            animated = False
        super()._add_geom(geom, state, matrix, tint, layer, animated)


def attachment(node, root):
    matrix = node.getMat(root)
    values = np.round([[matrix.getCell(column, row) for column in range(4)] for row in range(3)], 5)
    values[values == 0] = 0  # canonical positive zero in float attachment records
    return values.tolist()


def write_model(name, node, animated=False):
    extractor = StableBossExtractor(max_tex=128)
    extractor.add(node, animated=animated)
    stats = ttm.write(str(OUT / name), extractor.textures, extractor.batches())
    if stats['geometry_bytes'] > 6 * 1024 * 1024 or stats['texture_bytes'] > 8 * 1024 * 1024:
        raise ValueError(f'{name}: hardware zone budget exceeded')
    return extractor, stats


def bake_parts(base, report, prefix, character):
    source = ast.parse((UPSTREAM / 'toontown/suit/BossCog.py').read_text())
    clips = next(ast.literal_eval(node.value) for node in source.body
                 if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'AnimList'
                                                        for t in node.targets))
    generic = 'phase_9/models/char/bossCog'
    models = {'legs': generic + '-legs-zero', 'torso': character + '-torso-zero',
              'head': character + '-head-zero'}
    for part, model_path in models.items():
        animations = {clip: str(RESOURCES / f'{generic}-{part}-{clip}.bam') for clip in clips}
        for animation in animations.values():
            if not Path(animation).is_file():
                raise ValueError(f'Missing stock animation: {animation}')
        actor = Actor({part: str(RESOURCES / (model_path + '.bam'))}, {part: animations})
        node = actor.getPart(part)
        actor.pose('Ff_neutral', 0)
        actor.update(force=True)
        doors = []
        if part == 'legs':
            # BossCog.__setupDoor puts an extra transform below each joint.
            for joint_name, roll in [('joint_doorFront', -80), ('joint_doorRear', 80)]:
                door_joint = node.find('**/' + joint_name)
                children = door_joint.getChildren()
                hinge = door_joint.attachNewNode('baked_hatch')
                children.reparentTo(hinge)
                hinge.setR(roll)
                doors.append(hinge)
        extractor, stats = write_model(f'{prefix}_{part}.ttm', node, animated=True)
        report['models'][prefix + '_' + part] = stats
        vertices = stats['vertices']
        joint_name = 'joint_pelvis' if part == 'legs' else 'joint34' if part == 'torso' else None
        joint = node.find('**/' + joint_name) if joint_name else None
        if joint is not None and joint.isEmpty():
            raise ValueError(f'{part}: missing attachment {joint_name}')
        tread_node = None
        tread_holder = None
        if part == 'legs':
            axle = node.find('**/joint_axle')
            if axle.isEmpty():
                raise ValueError('VP legs have no axle joint')
            tread_node = base.loader.loadModel(str(RESOURCES / (generic + '-treads.bam')))
            tread_node.reparentTo(axle)
            tread_holder = NodePath('treads')
            tread_copy = tread_node.copyTo(tread_holder)
            tread_copy.setMat(tread_node.getMat(node))
            _, report['models'][prefix + '_treads'] = write_model(prefix + '_treads.ttm', tread_holder)
        for clip in list(clips) + ['hatch_front', 'hatch_rear']:
            source_clip = 'Fb_neutral' if clip.startswith('hatch_') else clip
            for index, door in enumerate(doors):
                door.setR(0 if clip == ('hatch_front' if index == 0 else 'hatch_rear') else (-80 if index == 0 else 80))
            frames = []
            attachments = []
            treads = []
            source_frames = actor.getNumFrames(source_clip, partName=part)
            source_fps = actor.getFrameRate(source_clip, partName=part)
            step = max(1, round(source_fps / 12))
            for frame in range(0, 1 if clip.startswith('hatch_') else source_frames, step):
                actor.pose(source_clip, frame, partName=part)
                actor.update(force=True)
                current = StableBossExtractor(max_tex=128, encode_textures=False)
                current.add(node, animated=True, skip_under=(tread_node,) if tread_node else ())
                positions = np.concatenate([ttm._rebase_uvs(batch).verts[:, :3] for batch in current.batches()])
                if len(positions) != vertices:
                    raise ValueError(f'{part}/{clip}: vertex order/count changed')
                frames.append(np.round(positions, 4))
                if joint is not None:
                    attachments.append(attachment(joint, node))
                if tread_node:
                    tread_copy.setMat(tread_node.getMat(node))
                    current = StableBossExtractor(max_tex=128, encode_textures=False)
                    current.add(tread_holder)
                    positions = np.concatenate([ttm._rebase_uvs(batch).verts[:, :3] for batch in current.batches()])
                    if len(positions) != report['models'][prefix + '_treads']['vertices']:
                        raise ValueError(f'treads/{clip}: vertex order/count changed')
                    treads.append(np.round(positions, 4))
            name = f'{prefix}_{part}_{clip}.tta'
            stats = tta.write(str(OUT / name), frames, source_fps / step, attachments or None)
            report['clips'][name] = dict(stats, source_frames=source_frames, source_fps=source_fps)
            if treads:
                name = f'{prefix}_treads_{clip}.tta'
                report['clips'][name] = tta.write(str(OUT / name), treads, source_fps / step)
        if tread_holder:
            tread_holder.removeNode()
        actor.cleanup()
    return clips

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    base = ShowBase()
    # NPCToons.createLocalNPC uses these DNA tuples, or newToonRandom(seed, gender, 1).
    sys.path.insert(0, str(UPSTREAM))
    from toontown.toon import ToonDNA
    npc_source = ast.parse((UPSTREAM / 'toontown/toon/NPCToons.py').read_text())
    dictionary = next(node.value for node in npc_source.body if isinstance(node, ast.Assign)
                      and any(isinstance(target, ast.Name) and target.id == 'NPCToonDict' for target in node.targets))
    descriptions = {ast.literal_eval(key): value for key, value in zip(dictionary.keys, dictionary.values)}
    for node in ast.walk(npc_source):
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Tuple):
            for target in node.targets:
                if isinstance(target, ast.Subscript) and isinstance(target.value, ast.Name) and target.value.id == 'NPCToonDict':
                    descriptions[ast.literal_eval(target.slice)] = node.value
    npc_ids = sorted(set(map(int, re.findall(r'\{ (\d+),', (ROOT / 'source/ui/sos_tables.h').read_text()))))
    records = []
    for npc_id in npc_ids:
        description = descriptions[npc_id]
        properties = ast.literal_eval(description.elts[2])
        dna = ToonDNA.ToonDNA()
        if properties == 'r':
            dna.newToonRandom(npc_id, ast.literal_eval(description.elts[3]), 1)
        else:
            dna.newToonFromProperties(*properties)
        net_string = dna.makeNetString()
        if isinstance(net_string, str):
            net_string = net_string.encode('latin1')
        if len(net_string) != 15:
            raise ValueError(f'NPC {npc_id}: unexpected DNA size')
        records.append(struct.pack('<I', npc_id) + net_string)
    (OUT / 'vp_npcs.bin').write_bytes(struct.pack('<4sHH', b'BND1', 1, len(records)) + b''.join(records))
    report = {'version': 1, 'sample_fps': 12, 'max_texture': 128, 'models': {}, 'clips': {}}
    arena = base.loader.loadModel(Filename.fromOsSpecific(str(RESOURCES / 'phase_9/models/cogHQ/BossRoomHQ.bam')))
    entrance = arena.find('**/elevatorEntrance')
    if entrance.isEmpty():
        raise ValueError('VP arena elevator entrance missing')
    entrance.getChildren().detach()
    entrance.setScale(1)
    elevator = base.loader.loadModel(str(RESOURCES / 'phase_9/models/cogHQ/cogHQ_elevator.bam'))
    elevator.reparentTo(entrance)
    # The stock client explicitly resets cage scale before positioning it.
    cage = arena.find('**/cage')
    cage.setScale(1)
    cage.setZ(100)
    # The cage and sliding ramps are queried separately from the static world.
    cage.stash()
    ramps = [arena.find('**/' + name) for name in ('west_ramp2', 'west_ramp', 'west_ramp1')]
    offsets = [arena.getRelativeVector(ramp, Vec3(18, 0, 0)) for ramp in ramps]
    for ramp in ramps:
        ramp.stash()
    arena_collision = collide.extract(arena, fallback_floor='auto')
    for ramp in ramps:
        ramp.unstash()
    cage.unstash()
    hinge = arena.find('**/cage_door').getPos(arena)
    (OUT / 'vp_scene.bin').write_bytes(struct.pack('<4sHH12f', b'BSC1', 1, 3,
                                                    *(value for vector in offsets for value in vector), *hinge))
    report['ramp_retract_offsets'] = [list(offset) for offset in offsets]
    report['cage_door_hinge'] = list(hinge)
    lobby = base.loader.loadModel(str(RESOURCES / 'phase_9/models/cogHQ/SellbotHQLobby.bam'))
    locator = lobby.find('**/elevator_locator')
    if locator.isEmpty():
        raise ValueError('VP lobby elevator locator missing')
    point = locator.getPos(lobby)
    (OUT / 'vp_lobby_anchor.txt').write_text(f'BPA1 1\n{point.x:.6f} {point.y:.6f} {point.z:.6f}\n')
    report['lobby_anchor'] = list(point)
    lobby.removeNode()
    # These nodes move separately. Preserve their original placement in each part.
    for name, pattern in [('vp_cage_door', '**/cage_door'), ('vp_cage', '**/cage'),
                          ('vp_ramp_a', '**/west_ramp2'), ('vp_ramp_b', '**/west_ramp'),
                          ('vp_ramp_c', '**/west_ramp1')]:
        node = arena.find(pattern)
        if node.isEmpty():
            raise ValueError(f'VP arena node missing: {pattern}')
        holder = NodePath(name)
        node.wrtReparentTo(holder)
        _, report['models'][name] = write_model(name + '.ttm', holder)
        mesh = collide.extract(holder, fallback_floor=False)
        if mesh.tris or mesh.caps:
            mesh.write(str(OUT / (name + '.ttc')), max_bytes=1024 * 1024)
        holder.removeNode()
    _, report['models']['vp_arena'] = write_model('vp_arena.ttm', arena)
    arena_collision.write(str(OUT / 'vp_arena.ttc'), max_bytes=1024 * 1024)
    arena.removeNode()

    clips = bake_parts(base, report, 'vp', 'phase_9/models/char/sellbotBoss')
    for name, path in [('vp_gear', 'phase_9/models/char/gearProp'),
                       ('vp_pie', 'phase_3.5/models/props/tart')]:
        node = base.loader.loadModel(str(RESOURCES / (path + '.bam')))
        _, report['models'][name] = write_model(name + '.ttm', node)
        node.removeNode()
    report['sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in sorted(OUT.glob('vp_*')) if path.suffix in ('.ttm', '.tta', '.ttc', '.bin', '.txt')}
    model_geometry = sum(model['geometry_bytes'] for model in report['models'].values())
    model_textures = sum(model['texture_bytes'] for model in report['models'].values())
    largest_clip = max(sum(report['clips'][f'vp_{part}_{clip}.tta']['frames'] *
                           report['clips'][f'vp_{part}_{clip}.tta']['vertices'] * 8
                           for part in ('legs', 'torso', 'head', 'treads'))
                       for clip in list(clips) + ['hatch_front', 'hatch_rear'])
    report['budget'] = dict(model_geometry=model_geometry, textures=model_textures,
                            max_clip=largest_clip, clip_pair_geometry=model_geometry + 2 * largest_clip,
                            triangles=sum(model['triangles'] for model in report['models'].values()),
                            batches=sum(model['batches'] for model in report['models'].values()))
    if model_geometry + 2 * largest_clip > 6 * 1024 * 1024 or model_textures > 8 * 1024 * 1024:
        raise ValueError('Combined VP arena / models / double-buffered clips exceed Old 3DS budget')
    report['disk_bytes'] = sum(path.stat().st_size for path in OUT.glob('vp_*') if path.suffix in ('.ttm', '.tta', '.ttc', '.bin', '.txt'))
    (OUT / 'vp_manifest.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(report, indent=2, sort_keys=True), flush=True)


def main_cfo():
    """CFO stock arena transforms; goons stay in the existing level/props set."""
    OUT.mkdir(parents=True, exist_ok=True)
    base = ShowBase()
    report = {'version': 1, 'sample_fps': 12, 'max_texture': 128, 'models': {}, 'clips': {}}
    scene = NodePath('cfo_arena')
    for model, position in [('MidVault', (0, -222, -70.7)), ('EndVault', (84, -201, -6))]:
        node = base.loader.loadModel(str(RESOURCES / f'phase_10/models/cogHQ/{model}.bam'))
        node.setPos(*position)
        node.reparentTo(scene)
        if model == 'EndVault':
            for pattern in ('MagnetArms', 'Safes', 'MagnetControlsAll'):
                node.findAllMatches('**/' + pattern).detach()
        else:
            origin = node.find('**/elevator_origin')
            if origin.isEmpty():
                raise ValueError('CFO elevator origin missing')
            origin.setScale(1)
            elevator = base.loader.loadModel(str(RESOURCES / 'phase_10/models/cogHQ/CFOElevator.bam'))
            elevator.reparentTo(origin)
    # Stock sliding doors open for travel between the two vaults.
    for door in scene.findAllMatches('**/SlidingDoor*'):
        if not door.node().isGeomNode() and 'Collision' not in door.getName():
            door.setZ(25)
    _, report['models']['cfo_arena'] = write_model('cfo_arena.ttm', scene)
    collision = collide.extract(scene, fallback_floor='auto')
    collision.write(str(OUT / 'cfo_arena.ttc'), max_bytes=1024 * 1024)
    scene.removeNode()
    lobby = base.loader.loadModel(str(RESOURCES / 'phase_10/models/cogHQ/VaultLobby.bam'))
    # DistributedCFOElevator.setupElevator parents its model to elevator_locator.
    locator = lobby.find('**/elevator_locator')
    if locator.isEmpty():
        raise ValueError('CFO lobby elevator locator missing')
    point = locator.getPos(lobby)
    (OUT / 'cfo_lobby_anchor.txt').write_text(f'BPA1 1\n{point.x:.6f} {point.y:.6f} {point.z:.6f}\n')
    report['lobby_anchor'] = list(point)
    lobby.removeNode()
    sys.path.insert(0, str(UPSTREAM))
    from toontown.toon import ToonDNA
    resistance = ToonDNA.ToonDNA()
    resistance.newToonRandom(146392, 'f', 1)
    resistance.head = 'pls'
    dna = resistance.makeNetString()
    if isinstance(dna, str):
        dna = dna.encode('latin1')
    (OUT / 'cfo_npcs.bin').write_bytes(struct.pack('<4sHHI', b'BND1', 1, 1, 146392) + dna)
    bake_parts(base, report, 'cfo', 'phase_10/models/char/cashbotBoss')
    props = [('crane_arm', 'CBCraneArm'), ('controls', 'CBCraneControls'),
             ('stick', 'CBCraneStick'), ('magnet', 'CBMagnet'), ('safe', 'CBSafe'),
             ('locomotive', 'CashBotLocomotive'), ('boxcar', 'CashBotBoxCar'), ('tankcar', 'CashBotTankCar')]
    for name, model in props:
        node = base.loader.loadModel(str(RESOURCES / f'phase_10/models/cogHQ/{model}.bam'))
        _, report['models']['cfo_' + name] = write_model('cfo_' + name + '.ttm', node)
        node.removeNode()
    for name, path in [('gear', 'phase_9/models/char/gearProp'),
                       ('treasure', 'phase_4/models/props/icecream')]:
        node = base.loader.loadModel(str(RESOURCES / (path + '.bam')))
        if node.isEmpty():
            raise ValueError(f'Missing CFO prop {path}')
        _, report['models']['cfo_' + name] = write_model('cfo_' + name + '.ttm', node)
        node.removeNode()
    from panda3d.core import Geom, GeomNode, GeomTriangles, GeomVertexFormat
    data = GeomVertexData('cfo_cable', GeomVertexFormat.getV3c4(), Geom.UHStatic)
    positions = GeomVertexWriter(data, 'vertex')
    colors = GeomVertexWriter(data, 'color')
    for z in (0, 1):
        for x, y in ((-.08, -.08), (.08, -.08), (.08, .08), (-.08, .08)):
            positions.addData3(x, y, z)
            colors.addData4(.12, .12, .12, 1)
    triangles = GeomTriangles(Geom.UHStatic)
    for side in range(4):
        following = (side + 1) % 4
        triangles.addVertices(side, following, side + 4)
        triangles.addVertices(following, following + 4, side + 4)
    geometry_node = Geom(data)
    geometry_node.addPrimitive(triangles)
    node = GeomNode('cfo_cable')
    node.addGeom(geometry_node)
    _, report['models']['cfo_cable'] = write_model('cfo_cable.ttm', NodePath(node))
    geometry = sum(model['geometry_bytes'] for model in report['models'].values())
    textures = sum(model['texture_bytes'] for model in report['models'].values())
    clips = {name.split('_', 2)[2][:-4] for name in report['clips']}
    largest = max(sum(report['clips'][f'cfo_{part}_{clip}.tta']['frames'] *
                      report['clips'][f'cfo_{part}_{clip}.tta']['vertices'] * 8
                      for part in ('legs', 'torso', 'head', 'treads')) for clip in clips)
    # Admission reserve for existing level/props goon model + three clips + cone.
    # Runtime validates their actual headers before loading anything.
    report['goon_reserve'] = {'geometry': 1835008, 'textures': 131072}
    report['budget'] = {'model_geometry': geometry, 'textures': textures,
                        'max_clip': largest, 'clip_pair_geometry': geometry + 2 * largest + 1835008,
                        'triangles': sum(model['triangles'] for model in report['models'].values()),
                        'batches': sum(model['batches'] for model in report['models'].values())}
    if geometry + 2 * largest + 1835008 > 6 * 1024 * 1024 or textures + 131072 > 8 * 1024 * 1024:
        raise ValueError('CFO combined geometry/clips/goon reserve exceeds Old 3DS budget')
    report['sha256'] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                        for path in sorted(OUT.glob('cfo_*')) if path.suffix in ('.ttm', '.tta', '.ttc', '.txt', '.bin')}
    report['disk_bytes'] = sum((OUT / name).stat().st_size for name in report['sha256'])
    (OUT / 'cfo_manifest.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(report, indent=2, sort_keys=True), flush=True)


if __name__ == '__main__':
    if '--cj' in sys.argv:
        import bake_cj
        bake_cj.main(sys.modules[__name__])
    else:
        main_cfo() if '--cfo' in sys.argv else main()
