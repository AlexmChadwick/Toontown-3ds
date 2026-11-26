#!/usr/bin/env python3
"""Bake the Cog HQ elevators that upstream loads at run time, not from the zone model.

Every HQ elevator is a DistributedElevator whose setupElevator loads its own model:

  type     class (zone)                                   model, placement
  factory  DistributedFactoryElevatorExt (11200)          phase_4 elevator, scale 1.05, light
                                                          panel removed; setEntranceId pos/hpr
  mint     DistributedMintElevatorExt (12000),            mintElevator at the HQ model's
           DistributedLawOfficeElevatorExt (13200)        elevator_origin_<n>; sign text
  vp       DistributedVPElevator (11100)                  cogHQ_elevator, big_frame hidden, H 180
  cfo      DistributedCFOElevator (12100)                 CFOElevator          } on the lobby's
  cj       DistributedCJElevator (13100)                  LB_Elevator          } elevator_locator
  bb       DistributedBBElevator (10100)                  BB_Elevator          }

The zone bakes hold only the HQ model, so without these the doorways were holes (sky through
the DA office doors, a black void in the CFO lobby, open arches at the factory). The doors
slide: ElevatorUtils puts them at (0, 0, 0) open and (+-width, 0, 0) closed (ElevatorData).
The mint and DA office elevators also write the destination on the HQ model's
ElevatorFrameFront_<n> (OnscreenText, suit font, at elevator_signorigin_<n>).

Writes data/cogbldg/hqelev_<type>_{frame,left,right}.ttm (elevator space, doors open) and per
zone data/zones/<zone>.thq plus, for 12000 / 13200, <zone>_hqsign.ttm (one batch per sign,
world space). THQ1 (little endian):
  char[4] "THQ1"; u16 version (1); u16 count
  count x { u8 type (0 factory, 1 mint, 2 vp, 3 cfo, 4 cj, 5 bb); i8 origin (entrance /
            elevator_origin index, -1 for a lobby's boss elevator); u8 sign batch (255: none);
            u8 pad; f32[12] world from elevator (rows: x axis, y axis, z axis, origin);
            f32 width; f32 openTime; f32 closeTime }
"""
import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))

TYPES = ['factory', 'mint', 'vp', 'cfo', 'cj', 'bb']
# model, nodes removed (setupElevator), ElevatorData width / openTime / closeTime
MODELS = {
    'factory': ('phase_4/models/modules/elevator', ('light_panel', 'light_panel_frame'), (3.5, 2.0, 2.0)),
    'mint': ('phase_10/models/cogHQ/mintElevator', (), (5.875, 2.0, 2.0)),   # ELEVATOR_MINT / OFFICE
    'vp': ('phase_9/models/cogHQ/cogHQ_elevator', ('big_frame',), (11.5, 4.0, 4.0)),
    'cfo': ('phase_10/models/cogHQ/CFOElevator', (), (8.166, 3.0, 3.0)),
    'cj': ('phase_11/models/lawbotHQ/LB_Elevator', (), (15.8, 4.0, 4.0)),
    'bb': ('phase_12/models/bossbotHQ/BB_Elevator', (), (6.3, 4.0, 4.0)),
}
# DistributedFactoryElevatorExt.setEntranceId (scale 1.05 from setupElevator).
FACTORY = [(62.74, -85.31, 0.0, 2.0), (-162.25, 26.43, 0.0, 269.0)]
# zone -> (type, placements); 'origins' reads elevator_origin_<n>, 'locator' elevator_locator.
ZONES = {
    11200: ('factory', 'factory'),
    12000: ('mint', 'origins'),
    13200: ('mint', 'origins'),
    11100: ('vp', 'locator'),
    12100: ('cfo', 'locator'),
    13100: ('cj', 'locator'),
    10100: ('bb', 'locator'),
}
# setMintId: mint zone -> origin; setEntranceId: entrance -> stage zone. The sign is
# TextEncoder.upper(GlobalStreetNames[zone][-1]) at OnscreenText scale DMEEsignText (2) / 2.
SIGNS = {
    12000: {1: 'CASHBOT COIN MINT', 2: 'CASHBOT DOLLAR MINT', 0: 'CASHBOT BULLION MINT'},
    13200: {0: 'LAWBOT A OFFICE', 1: 'LAWBOT B OFFICE', 2: 'LAWBOT C OFFICE', 3: 'LAWBOT D OFFICE'},
}
SIGN_SCALE = 2.0
SIGN_COLOR = (0.87, 0.87, 0.87, 1.0)


def _rows(m):
    return [round(float(m.getCell(r, c)), 5) + 0.0 for r in range(4) for c in range(3)]


def _base():
    from panda3d.core import loadPrcFileData, BamCache
    BamCache.getGlobalPtr().setActive(False)
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                        'textures-power-2 none\nmodel-cache-dir')
    from direct.showbase.ShowBase import ShowBase
    return ShowBase()


def _load(base, resources, rel):
    from panda3d.core import Filename
    model = base.loader.loadModel(Filename.fromOsSpecific(str(Path(resources) / (rel + '.bam'))), noCache=True)
    if model is None or model.isEmpty():
        raise FileNotFoundError(rel)
    return model


def _write(path, ex, batches=None):
    import ttm
    stats = ttm.write(str(path), ex.textures, ex.batches() if batches is None else batches)
    if stats['uv_clamped']:
        raise ValueError(f'{path}: UV overflow')
    return stats


def _door(model, side):
    for name in (f'{side}-door', f'{side}_door'):
        node = model.find('**/' + name)
        if not node.isEmpty():
            return node
    raise RuntimeError(f'{model.getName()}: no {side} door')


def bake_global(base, resources, out):
    import extract
    from panda3d.core import LMatrix4f, NodePath
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for t in TYPES:
        rel, removed, _ = MODELS[t]
        model = _load(base, resources, rel)
        for name in removed:
            node = model.find('**/' + name)
            if node.isEmpty():
                raise RuntimeError(f'{rel}: missing {name}')
            node.removeNode()
        doors = {side: _door(model, side) for side in ('left', 'right')}
        for side, node in doors.items():
            if not node.getMat(model).almostEqual(LMatrix4f.identMat(), 1e-4):
                raise RuntimeError(f'{rel}: {side} door does not slide along the elevator x axis')
        for side, node in doors.items():
            holder = NodePath(side)
            node.copyTo(holder)
            ex = extract.Extractor(max_tex=256)
            ex.add(holder)
            report[f'hqelev_{t}_{side}'] = _write(out / f'hqelev_{t}_{side}.ttm', ex)
            holder.removeNode()
            node.stash()
        ex = extract.Extractor(max_tex=256)
        ex.add(model)
        report[f'hqelev_{t}_frame'] = _write(out / f'hqelev_{t}_frame.ttm', ex)
        model.removeNode()
    return report


def bake_zone(base, resources, zone, out):
    import coghq_catalog
    import extract
    from panda3d.core import DynamicTextFont, Filename, NodePath, TextNode
    kind, how = ZONES[zone]
    root = NodePath('root')
    placements = []  # (origin, matrix)
    if how == 'factory':
        for i, (x, y, z, h) in enumerate(FACTORY):
            n = root.attachNewNode('e')
            n.setPosHpr(x, y, z, h, 0, 0)
            n.setScale(1.05)
            placements.append((i, n.getMat(root)))
    else:
        geom = _load(base, resources, coghq_catalog.CATALOG[zone]['model'][:-len('.bam')])
        geom.reparentTo(root)
        if how == 'origins':
            for loc in geom.findAllMatches('**/elevator_origin_*'):
                suffix = loc.getName()[len('elevator_origin_'):]
                if not suffix.isdigit():
                    continue
                n = root.attachNewNode('e')
                n.setPosHpr(loc, 0, 0, 0, 0, 0, 0)   # setPosHpr(locator, 0...): no locator scale
                placements.append((int(suffix), n.getMat(root)))
        else:
            loc = geom.find('**/elevator_locator')
            if loc.isEmpty():
                raise RuntimeError(f'{zone}: no elevator_locator')
            n = loc.attachNewNode('e')      # reparentTo(locator): inherits its scale
            if kind == 'vp':
                n.setH(180)
            placements.append((-1, n.getMat(root)))
    placements.sort(key=lambda p: p[0])
    if not placements:
        raise RuntimeError(f'{zone}: no elevator placements')

    sign_batches, sign_of = [], {}
    if zone in SIGNS:
        font = DynamicTextFont(Filename.fromOsSpecific(str(Path(resources) / 'phase_3/models/fonts/vtRemingtonPortable.ttf')))
        font.setPixelsPerUnit(40)
        font.setSpaceAdvance(0.25)
        font.setLineHeight(1.0)
        nodes = []
        for origin, _ in placements:
            text = SIGNS[zone].get(origin)
            loc = geom.find(f'**/elevator_signorigin_{origin}')
            if text is None or loc.isEmpty():
                continue
            tn = TextNode('sign')
            tn.setFont(font)
            tn.setAlign(TextNode.ACenter)
            tn.setTextColor(*SIGN_COLOR)
            tn.setText(text)
            holder = NodePath('sign')
            at = holder.attachNewNode('at')
            at.setMat(loc.getMat(root))
            letters = at.attachNewNode(tn.generate())
            letters.setScale(SIGN_SCALE, 1, SIGN_SCALE)
            nodes.append((origin, holder))
        ex = extract.Extractor(max_tex=256)
        for origin, holder in nodes:  # every glyph is on the font pages before any is read
            ex._groups = {}
            ex.add(holder)
            bs = ex.batches()
            if len(bs) != 1:
                raise RuntimeError(f'{zone} sign {origin}: {len(bs)} batches')
            bs[0].layer = 2
            sign_of[origin] = len(sign_batches)
            sign_batches.append(bs[0])
    sign_stats = _write(out / f'{zone}_hqsign.ttm', ex, sign_batches) if sign_batches else None

    width, open_t, close_t = MODELS[kind][2]
    with open(out / f'{zone}.thq', 'wb') as f:
        f.write(struct.pack('<4sHH', b'THQ1', 1, len(placements)))
        for origin, m in placements:
            f.write(struct.pack('<BbBB12f3f', TYPES.index(kind), origin, sign_of.get(origin, 255), 0,
                                *_rows(m), width, open_t, close_t))
    root.removeNode()
    return {'zone': zone, 'type': kind, 'elevators': [p[0] for p in placements], 'signs': sign_stats}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--resources', default=str(ROOT / 'ref/resources'))
    ap.add_argument('--data', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data'))
    ap.add_argument('--global', dest='do_global', action='store_true')
    ap.add_argument('--zone', default='', help='zone ids, comma separated, or "all"')
    args = ap.parse_args()
    zones = sorted(ZONES) if args.zone == 'all' else [int(z) for z in args.zone.split(',') if z]
    if len(zones) + args.do_global > 1:
        # One process per output (bake_cog_exterior.py): each bakes to the same bytes alone.
        import subprocess
        common = ['--resources', args.resources, '--data', args.data]
        if args.do_global:
            subprocess.run([sys.executable, '-I', __file__, '--global', *common], check=True)
        for z in zones:
            subprocess.run([sys.executable, '-I', __file__, '--zone', str(z), *common], check=True)
        return
    base = _base()
    data = Path(args.data)
    if args.do_global:
        print(json.dumps(bake_global(base, args.resources, data / 'cogbldg')), flush=True)
    for z in zones:
        print(json.dumps(bake_zone(base, args.resources, z, data / 'zones')), flush=True)
    base.destroy()


if __name__ == '__main__':
    main()
