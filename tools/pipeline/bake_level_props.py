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


def main():
    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    resources = default_resources()
    out = ROOT / 'sdmc-out/3ds/toontown3ds/data/level/props'
    out.mkdir(parents=True, exist_ok=True)
    report = {'door': bake_door(base, resources, out), 'button': bake_button(base, resources, out)}
    (out / 'manifest.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
    print(json.dumps({k: {p: (v.get('triangles') if isinstance(v, dict) else v) for p, v in r.items()}
                      for k, r in report.items()}, indent=2, default=str))
    base.destroy()


if __name__ == '__main__':
    main()
