#!/usr/bin/env python3
"""Bake the moving / interactive props of facility levels (docs/design/levels.md).

    .venv/bin/python -I tools/pipeline/bake_level_props.py

Outputs sdmc-out/3ds/toontown3ds/data/level/props/<name>.ttm (+ <name>.box: the blocking
box of a closed door, "minx miny minz maxx maxy maxz" in the prop's root space).

Every part is baked in the space of the model root, exactly as the upstream client attaches
it under the entity node, so the runtime only adds the part's animated offset:
  door_*   DistributedDoorEntity.setupDoor (CogDoorHandShake: frame, top/bottom outer doors,
           left/right inner doors, three lock slides x closed / left-open / right-open)
  button_* DistributedButton.setupSwitch (CogDoor_Button: base and the pressable top)
  stomper_*, stomper_lb_*  DistributedStomper.loadModel (square_stomper / LB_square_stomper)
  lift     DistributedLift.initPlatform (Elevator: floor model, floor/ceiling/wall boxes)
  barrel*, gag_icon_<track>_<level>  DistributedBarrelBase (gagTank + its labels)
  gear_*   GearEntity; paint_mixer_* PaintMixer / PlatformEntity; tread ConveyorBelt
  goon_pg_*, goon_sg_*, goon_cone  DistributedGoon (Cog_Goonie per hat + clips, radar cone)
Deterministic; at most 3 ETC1 encoder threads.
"""
import json
import os
from pathlib import Path
import sys
sys.dont_write_bytecode = True

os.environ.setdefault('TT_ETC1_THREADS', '3')
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent

DOOR_PARTS = {
    'top': 'doortop', 'bottom': 'doorbottom', 'left': 'doorLeft', 'right': 'doorRight',
    **{f'lock{i}_{state}': f'Slide_{word}_{node}' for i, word in ((1, 'One'), (2, 'Two'), (3, 'Three'))
       for state, node in (('closed', 'Closed'), ('left', 'Left_Open'), ('right', 'Right_Open'))},
}


def default_resources():
    for c in (ROOT / 'ref/resources', ROOT / 'ref/ref/resources'):
        if (c / 'phase_9').exists():
            return c
    return ROOT / 'ref/resources'


def write_part(model, node, out):
    import extract
    import ttm
    ex = extract.Extractor(max_tex=128)
    ex.add(node if node is not None else model)
    batches = ex.batches()
    if node is not None:
        # Extractor works relative to the node it is given; put the part back in model space.
        mat = node.getMat(model)
        if not mat.isIdentity():
            for b in batches:
                b.verts[:, :3] = [tuple(mat.xformPoint(tuple(p))) for p in b.verts[:, :3]]
    return ttm.write(str(out), ex.textures, batches)


def bake_door(base, resources, out):
    model = base.loader.loadModel(str(resources / 'phase_9/models/cogHQ/CogDoorHandShake.bam'), noCache=True)
    doorway = model.find('**/Doorway1')
    report = {}
    parts = {}
    for name, node in DOOR_PARTS.items():
        n = doorway.find('**/' + node)
        if n.isEmpty():
            raise ValueError(f'CogDoorHandShake: {node} missing')
        parts[name] = n
    # The frame: everything that never moves (DoorFrame_all, pocket); parts are baked alone.
    for n in parts.values():
        n.stash()
    report['door_frame'] = write_part(model, doorway, out / 'door_frame.ttm')
    for n in parts.values():
        n.unstash()
    for name, n in parts.items():
        report[f'door_{name}'] = write_part(model, n, out / f'door_{name}.ttm')
    # Closed doors block the doorway: the union of the outer and inner door leaves.
    lo, hi = None, None
    for name in ('top', 'bottom', 'left', 'right'):
        a, b = parts[name].getTightBounds(model)
        lo = a if lo is None else lo.fmin(a)
        hi = b if hi is None else hi.fmax(b)
    (out / 'door.box').write_text(' '.join(f'{v:.3f}' for v in (*lo, *hi)) + '\n')
    report['door_box'] = [list(lo), list(hi)]
    model.removeNode()
    return report


def bake_button(base, resources, out):
    """DistributedButton.setupSwitch: CogDoor_Button's buttonBase (frame) and button (the part
    that sinks 0.2 and turns green). button.box: the base's bounds; its top is a floor."""
    model = base.loader.loadModel(str(resources / 'phase_9/models/cogHQ/CogDoor_Button.bam'), noCache=True)
    report = {}
    lo, hi = None, None
    for name, node in (('button_base', 'buttonBase'), ('button_top', 'button')):
        n = model.find('**/' + node)
        if n.isEmpty():
            raise ValueError(f'CogDoor_Button: {node} missing')
        report[name] = write_part(model, n, out / f'{name}.ttm')
        a, b = n.getTightBounds(model)
        lo = a if lo is None else lo.fmin(a)
        hi = b if hi is None else hi.fmax(b)
    # The upstream floor top (0.475) when the model has no collision_floor of its own.
    hi = type(hi)(hi[0], hi[1], max(hi[2], 0.475))
    (out / 'button.box').write_text(' '.join(f'{v:.3f}' for v in (*lo, *hi)) + '\n')
    report['button_box'] = [list(lo), list(hi)]
    model.removeNode()
    return report


def collision_box(node, root):
    from panda3d.core import CollisionPolygon
    pts = []
    for cn in [node] + list(node.findAllMatches('**/+CollisionNode')):
        if not cn.node().isCollisionNode():
            continue
        mat = cn.getMat(root)
        for i in range(cn.node().getNumSolids()):
            solid = cn.node().getSolid(i)
            if isinstance(solid, CollisionPolygon):
                pts += [mat.xformPoint(solid.getPoint(k)) for k in range(solid.getNumPoints())]
    return [min(p[k] for p in pts) for k in range(3)] + [max(p[k] for p in pts) for k in range(3)]


def bake_lift(base, resources, out):
    """DistributedLift.initPlatform: the Elevator model (its floor is all that is drawn) and its
    collision boxes: elevator_floor, ceiling and the four walls, the walls after
    initPlatform's setScale(1, 1, 2) / setZ(-10). lift.box: one "name minx miny minz maxx maxy
    maxz" line per box, model space."""
    model = base.loader.loadModel(str(resources / 'phase_9/models/cogHQ/Elevator.bam'), noCache=True)
    report = {'lift': write_part(model, model.find('**/geometry'), out / 'lift.ttm')}
    lines = []
    coll = model.find('**/collsion')
    for name in ('elevator_floor', 'ceiling', 'wall_front', 'wall_back', 'wall_left', 'wall_right'):
        n = coll.find(name)
        if n.isEmpty():
            raise ValueError(f'Elevator: {name} missing')
        if name.startswith('wall_'):
            n.setScale(1.0, 1.0, 2.0)
            n.setZ(-10)
        box = collision_box(n, model)
        lines.append(name + ' ' + ' '.join(f'{v:.3f}' for v in box))
    (out / 'lift.box').write_text('\n'.join(lines) + '\n')
    report['lift_boxes'] = lines
    model.removeNode()
    return report


def av_props_new():
    """ToontownBattleGlobals.AvPropsNew: the inventory_icons node name per (track, level)."""
    import ast
    import level_spec_load
    tree = ast.parse((level_spec_load.UPSTREAM / 'toontown/toonbase/ToontownBattleGlobals.py').read_text())
    for n in tree.body:
        if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'AvPropsNew' for t in n.targets):
            return ast.literal_eval(n.value)
    raise ValueError('AvPropsNew not found')


def bake_barrels(base, resources, out):
    """DistributedBarrelBase.loadModel / applyLabel: the gagTank barrel, and its labels: a gag
    icon card per (track, level) for gag barrels (inventory_icons), the Jar for bean barrels
    (purchase_gui) and the icecream for heal barrels. barrel.box: the tank's collision tube."""
    report = {}
    tank = base.loader.loadModel(str(resources / 'phase_4/models/cogHQ/gagTank.bam'), noCache=True)
    label = tank.find('**/gagLabelDCS')
    if not label.isEmpty():
        label.setColor(0.15, 0.15, 0.1)  # dcsNode.setColor(0.15, 0.15, 0.1)
    report['barrel'] = write_part(tank, None, out / 'barrel.ttm')
    # It collides as its tube (tubeColl: a capsule along Z); its square is the solid box.
    tube = tank.find('**/tubeColl').node().getSolid(0)
    r, za, zb = tube.getRadius(), tube.getPointA()[2], tube.getPointB()[2]
    (out / 'barrel.box').write_text(' '.join(f'{v:.3f}' for v in (-r, -r, min(za, zb), r, r, max(za, zb))) + '\n')
    tank.removeNode()
    gui = base.loader.loadModel(str(resources / 'phase_4/models/gui/purchase_gui.bam'), noCache=True)
    jar = gui.find('**/Jar')
    report['barrel_jar'] = write_part(gui, jar, out / 'barrel_jar.ttm')
    gui.removeNode()
    ice = base.loader.loadModel(str(resources / 'phase_4/models/props/icecream.bam'), noCache=True)
    p = ice.find('**/p1_2')
    if not p.isEmpty():
        p.clearBillboard()
    report['barrel_icecream'] = write_part(ice, None, out / 'barrel_icecream.ttm')
    ice.removeNode()
    icons = base.loader.loadModel(str(resources / 'phase_3.5/models/gui/inventory_icons.bam'), noCache=True)
    for track, names in enumerate(av_props_new()):
        for level, name in enumerate(names):
            n = icons.find('**/' + name)
            if n.isEmpty():
                raise ValueError(f'inventory_icons: {name} missing')
            copy = n.copyTo(icons)
            copy.clearTransform()  # applyLabel reparents the node: its own transform is replaced
            report[f'gag_icon_{track}_{level}'] = write_part(icons, copy, out / f'gag_icon_{track}_{level}.ttm')
            copy.removeNode()
    icons.removeNode()
    return report


def bake_gears(base, resources, out):
    """GearEntity.ModelPaths: FactoryGearB / MintGear (scaled by gearScale and spun at runtime).
    gear_<type>.box: the disc's bounds in model space (radius in X/Y, thickness in Z)."""
    report = {}
    for name, path in (('factory', 'phase_9/models/cogHQ/FactoryGearB.bam'),
                       ('mint', 'phase_10/models/cashbotHQ/MintGear.bam')):
        model = base.loader.loadModel(str(resources / path), noCache=True)
        report[f'gear_{name}'] = write_part(model, None, out / f'gear_{name}.ttm')
        lo, hi = model.getTightBounds()
        (out / f'gear_{name}.box').write_text(' '.join(f'{v:.4f}' for v in (*lo, *hi)) + '\n')
        model.removeNode()
    return report


def bake_paint_mixer(base, resources, out):
    """PaintMixer.start: the top (cup) and the base shaft that shaftScale stretches about
    PaintMixerBase1. paint_mixer.box: lines "floor|wall minx miny minz maxx maxy maxz" (model
    space) and "shaft_origin x y z"."""
    model = base.loader.loadModel(str(resources / 'phase_9/models/cogHQ/PaintMixer.bam'), noCache=True)
    top = model.find('**/PaintMixerTop/+GeomNode')
    shaft = model.find('**/PaintMixerBase1')
    report = {'paint_mixer_top': write_part(model, top, out / 'paint_mixer_top.ttm')}
    origin = shaft.getPos(model)
    copy = shaft.copyTo(model)
    copy.clearTransform()  # its geometry in PaintMixerBase1's own space
    report['paint_mixer_shaft'] = write_part(model, copy, out / 'paint_mixer_shaft.ttm')
    copy.removeNode()
    lines = ['floor ' + ' '.join(f'{v:.3f}' for v in collision_box(model.find('**/PaintMixerFloorCollision'), model))]
    for wall in ('PaintMixerSideCollision', 'PaintMixerBaseSideCollision'):
        n = model.find('**/' + wall)
        if not n.isEmpty():
            lines.append('wall ' + ' '.join(f'{v:.3f}' for v in collision_box(n, model)))
    lines.append('shaft_origin ' + ' '.join(f'{v:.4f}' for v in origin))
    (out / 'paint_mixer.box').write_text('\n'.join(lines) + '\n')
    report['paint_mixer_boxes'] = lines
    model.removeNode()
    return report


def bake_tread(base, resources, out):
    """ConveyorBelt.initBelt: platform1 (setSx(widthScale) at runtime) and its floor box."""
    model = base.loader.loadModel(str(resources / 'phase_9/models/cogHQ/platform1.bam'), noCache=True)
    report = {'tread': write_part(model, model.find('**/platform'), out / 'tread.ttm')}
    box = collision_box(model.find('**/platformcollision'), model)
    (out / 'tread.box').write_text(' '.join(f'{v:.3f}' for v in box) + '\n')
    report['tread_box'] = box
    model.removeNode()
    return report


def bake_goons(base, resources, out):
    """Goon.generateGoon / createHead: Cog_Goonie with its walk / collapse / recovery clips (12 fps
    vertex morphs, battle_common.bake_actor), one bake per hat: 'pg' shows the hard hat, 'sg'
    the security hat. The actor geometry turns 180 and the eye is yellow, as upstream.
    goon_cone: the radar beam (alphaCone2's transform children, Goon.scaleRadar)."""
    import battle_common
    report = {}
    clips = {'walk': 'phase_9/models/char/Cog_Goonie-walk', 'collapse': 'phase_9/models/char/Cog_Goonie-collapse',
             'recovery': 'phase_9/models/char/Cog_Goonie-recovery'}
    for kind, hidden in (('pg', 'security_hat'), ('sg', 'hard_hat')):
        def prepare(actor, hidden=hidden):
            # Goon.initGoon: find('**/actorGeom').setH(180). On a bare Actor that geometry root is
            # __Actor_modelRoot (rotating the actor itself would be invisible to the bake).
            actor.find('__Actor_modelRoot').setH(180)
            hat = actor.find('**/joint8')
            other = hat.find('**/' + hidden)
            if not other.isEmpty():
                other.stash()
            eye = actor.find('**/eye')
            if not eye.isEmpty():
                eye.setColor(1, 1, 0, 1)
        report[f'goon_{kind}'] = battle_common.bake_actor(base, out, f'goon_{kind}', 'phase_9/models/char/Cog_Goonie-zero',
                                                          clips, {'full': 1500}, prepare)
    # The radar hangs off the eye (Goon.scaleRadar: radar at (0, -0.5, 0.4) in eye space).
    from direct.actor.Actor import Actor
    actor = Actor('phase_9/models/char/Cog_Goonie-zero', {'walk': clips['walk']})
    actor.find('__Actor_modelRoot').setH(180)
    actor.pose('walk', 0)
    actor.update(force=True)
    eye = actor.find('**/eye')
    p = actor.getRelativePoint(eye, (0, -0.5, 0.4))
    m = eye.getMat(actor)
    (out / 'goon.box').write_text('radar ' + ' '.join(f'{v:.4f}' for v in p) + '\n' +
                                  'eye_basis ' + ' '.join(f'{m.getCell(r, c):.4f}' for r in range(3) for c in range(3)) + '\n')
    report['goon_radar'] = list(p)
    actor.cleanup()
    actor.removeNode()
    cone = base.loader.loadModel(str(resources / 'phase_9/models/cogHQ/alphaCone2.bam'), noCache=True)
    # scaleRadar reparents the transform node's children to the beam (dropping its own matrix).
    beam = cone.find('**/transform').copyTo(cone)
    beam.clearTransform()
    report['goon_cone'] = write_part(cone, beam, out / 'goon_cone.ttm')
    beam.removeNode()
    cone.removeNode()
    return report


def bake_stomper(base, resources, out, name, path):
    """DistributedStomper.loadModel: square_stomper's head (scaled by headScale) and the shaft
    under it (offset, then shaftScale). Head baked in model space; shaft in its node's space.
    stomper.box: the head bounds and the shaft node's offset (model space)."""
    model = base.loader.loadModel(str(resources / path), noCache=True)
    head_geom = model.find('**/head/+GeomNode')
    shaft = model.find('**/head/shaft')
    if head_geom.isEmpty() or shaft.isEmpty():
        raise ValueError(f'{path}: head/shaft missing')
    report = {f'{name}_head': write_part(model, head_geom, out / f'{name}_head.ttm')}
    offset = shaft.getPos(model.find('**/head'))
    shaft_geom = shaft.find('+GeomNode') if not shaft.find('+GeomNode').isEmpty() else shaft
    # The shaft geometry in the shaft ModelNode's own space (the runtime applies the offset).
    shaft_copy = shaft.copyTo(model)
    shaft_copy.clearTransform()
    report[f'{name}_shaft'] = write_part(model, shaft_copy, out / f'{name}_shaft.ttm')
    shaft_copy.removeNode()
    lo, hi = head_geom.getTightBounds(model)
    (out / f'{name}.box').write_text(' '.join(f'{v:.4f}' for v in (*lo, *hi, *offset)) + '\n')
    report[f'{name}_box'] = [list(lo), list(hi), list(offset)]
    model.removeNode()
    return report


# Facility entity sounds (paths as the upstream classes load them) -> data/audio/<stem>.tsnd.
LEVEL_SFX = (
    'phase_4/audio/sfx/CHQ_FACT_stomper_small.ogg',      # DistributedStomper.stomperSounds
    'phase_9/audio/sfx/CHQ_FACT_stomper_med.ogg',
    'phase_9/audio/sfx/CHQ_FACT_stomper_large.ogg',
    'phase_9/audio/sfx/CHQ_FACT_door_open_sliding.ogg',  # DistributedDoorEntity
    'phase_9/audio/sfx/CHQ_FACT_door_open_final.ogg',
    'phase_9/audio/sfx/CHQ_FACT_door_unlock.ogg',        # DistributedDoorEntityLock
    'phase_9/audio/sfx/CHQ_FACT_arms_retracting.ogg',
    'phase_9/audio/sfx/CHQ_FACT_switch_pressed.ogg',     # DistributedButton
    'phase_9/audio/sfx/CHQ_FACT_switch_depressed.ogg',
    'phase_9/audio/sfx/CHQ_FACT_switch_popup.ogg',
    'phase_9/audio/sfx/toon_decompress.ogg',             # Toon.enterSquish
    'phase_9/audio/sfx/CHQ_FACT_elevator_up_down.ogg',   # DistributedLift
    'phase_4/audio/sfx/SZ_DD_treasure.ogg',              # DistributedBarrelBase grab
    'phase_4/audio/sfx/ring_miss.ogg',                   # ... reject
    'phase_9/audio/sfx/CHQ_GOON_hunker_down.ogg',        # DistributedGoon collapse
    'phase_9/audio/sfx/CHQ_GOON_rattle_shake.ogg',       # ... recover
    'phase_9/audio/sfx/CHQ_GOON_tractor_beam_alarmed.ogg',  # ... attack
)


def bake_sfx(resources, audio_out):
    import bake_audio
    audio_out.mkdir(parents=True, exist_ok=True)
    report = {}
    for src in LEVEL_SFX:
        dst = audio_out / (Path(src).stem + '.tsnd')
        size = bake_audio.bake_sfx_adpcm(str(resources / src), str(dst), False)[0]
        report[dst.name] = size
    return report


def main():
    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    resources = default_resources()
    out = ROOT / 'sdmc-out/3ds/toontown3ds/data/level/props'
    out.mkdir(parents=True, exist_ok=True)
    report = {'door': bake_door(base, resources, out), 'button': bake_button(base, resources, out),
              'stomper': bake_stomper(base, resources, out, 'stomper', 'phase_9/models/cogHQ/square_stomper.bam'),
              'stomper_lb': bake_stomper(base, resources, out, 'stomper_lb',
                                         'phase_11/models/lawbotHQ/LB_square_stomper.bam'),
              'lift': bake_lift(base, resources, out),
              'barrels': bake_barrels(base, resources, out),
              'gears': bake_gears(base, resources, out),
              'paint_mixer': bake_paint_mixer(base, resources, out),
              'tread': bake_tread(base, resources, out),
              'goons': bake_goons(base, resources, out)}
    report['sfx'] = bake_sfx(resources, out.parent.parent / 'audio')
    (out / 'manifest.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
    print(json.dumps({k: {p: (v.get('triangles') if isinstance(v, dict) else v) for p, v in r.items()}
                      for k, r in report.items()}, indent=2, default=str))
    base.destroy()


if __name__ == '__main__':
    main()
