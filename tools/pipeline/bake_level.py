#!/usr/bin/env python3
"""Bake one upstream level spec (Sellbot factory, Lawbot DA office, mint/stage/country-club
rooms) to the runtime formats.

    .venv/bin/python -I tools/pipeline/bake_level.py --level sellbot_factory [--tlv-only]
    .venv/bin/python -I tools/pipeline/bake_level.py --level all

Outputs under sdmc-out/3ds/toontown3ds/data/level/:
  <level>.tlv   entity table (tlv.py): every spec entity with its world transform and
                attributes, plus synthetic cogSpec / reserveCogSpec / battleCell entries.
  <level>.tz    (unless --tlv-only) the level model as TZ v2 with one chunk per spec zone,
                vis lists from the zone entities' 'visibility', floors tagged with the zone
                number (the runtime maps zone numbers to the AI's zone ids from setZoneIds).
  <level>.json  budget / content report.

Transforms mirror the client: zone entities are the model's '**/zone<num>;+i' nodes
(otp/level/LevelUtil.getZoneNum2Node), zone 0 is the model root, and every entity with
pos/hpr/scale is a node under its parent entity (BasicEntities.NodePathAttribs). Cog
spawn poses are spec 'pos'/'h' relative to the parent entity (DistributedFactorySuit.
setCogSpec). Deterministic; run with -I.
"""
import argparse
import json
import os
from pathlib import Path
import sys
sys.dont_write_bytecode = True

os.environ.setdefault('TT_ETC1_THREADS', '3')
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent

# level name -> (spec module, cog module or None). Mint/stage/country-club rooms are added
# with their room ids by the facility bakes; the factory-style levels are listed here.
LEVELS = {
    'sellbot_factory': ('SellbotLegFactorySpec', 'SellbotLegFactoryCogs'),
}


def room_levels():
    """Mint, stage and country-club rooms: <Name>RoomId2RoomName in *RoomSpecs.py; a room's
    Cogs are <room>_Cogs.py when present (MintRoomSpecs.CogSpecModules), else NullCogs."""
    import ast
    import level_spec_load
    out = {}
    for specs in ('MintRoomSpecs', 'StageRoomSpecs', 'CountryClubRoomSpecs'):
        tree = ast.parse((level_spec_load.COGHQ / f'{specs}.py').read_text())
        for node in tree.body:
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id.endswith('RoomId2RoomName'):
                for name in ast.literal_eval(node.value).values():
                    cogs = f'{name}_Cogs'
                    out[name] = (name, cogs if (level_spec_load.COGHQ / f'{cogs}.py').exists() else None)
    return out


def default_resources():
    for c in (ROOT / 'ref/resources', ROOT / 'ref/ref/resources', Path('/Users/alexchadwick/toontown-3ds/ref/resources')):
        if (c / 'phase_9').exists():
            return c
    return ROOT / 'ref/resources'


def zone_nodes(model):
    """otp/level/LevelUtil.getZoneNum2Node."""
    out = {}
    for n in model.findAllMatches('**/zone*;+i'):
        name = n.getName()[4:]
        digits = 0
        while digits < len(name) and name[digits].isdigit():
            digits += 1
        if not digits:
            continue
        num = int(name[:digits])
        if num == 0 or num > 999 or num in out:
            continue
        out[num] = n
    out[0] = model
    return out


def mat12(np_, root):
    m = np_.getMat(root)
    return [m.getCell(r, c) for r in range(4) for c in range(3)]


def vec(v, default):
    if v is None:
        return list(default)
    if isinstance(v, (int, float)):
        return [float(v)] * 3
    return [float(v[i]) for i in range(3)]


def build_tlv(ents, cogs, model, out_path):
    from panda3d.core import NodePath
    import tlv
    zones = zone_nodes(model)
    nodes = {}

    def node_of(ent_id, seen=()):
        if ent_id in nodes:
            return nodes[ent_id]
        spec = ents.get(ent_id)
        if spec is None:
            return model
        if spec['type'] == 'zone':
            n = zones.get(ent_id)
            if n is None:
                raise ValueError(f'zone {ent_id} missing from model')
            nodes[ent_id] = n
            return n
        if ent_id in seen:
            raise ValueError(f'parent cycle at {ent_id}')
        parent = node_of(spec.get('parentEntId', 0), seen + (ent_id,)) if ent_id != 0 else model
        if any(k in spec for k in ('pos', 'hpr', 'scale')):
            n = parent.attachNewNode(f'ent{ent_id}')
            n.setPos(*vec(spec.get('pos'), (0, 0, 0)))
            n.setHpr(*vec(spec.get('hpr'), (0, 0, 0)))
            n.setScale(*vec(spec.get('scale'), (1, 1, 1)))
        else:
            n = parent
        nodes[ent_id] = n
        return n

    def zone_of(ent_id):
        seen = set()
        while ent_id not in seen:
            seen.add(ent_id)
            spec = ents.get(ent_id)
            if spec is None:
                return 0
            if spec['type'] == 'zone':
                return ent_id
            ent_id = spec.get('parentEntId', 0)
        return 0

    w = tlv.Writer()
    skip = {'type', 'parentEntId', 'pos', 'hpr', 'scale', 'comment', 'name'}
    for ent_id in sorted(ents):
        spec = ents[ent_id]
        n = node_of(ent_id)
        attrs = {k: v for k, v in spec.items() if k not in skip}
        attrs['name'] = str(spec.get('name', ''))
        w.add(ent_id, spec.get('parentEntId', 0) if ent_id else 0, zone_of(ent_id), spec['type'], mat12(n, model),
              vec(spec.get('pos'), (0, 0, 0)), vec(spec.get('hpr'), (0, 0, 0)), vec(spec.get('scale'), (1, 1, 1)),
              attrs)
    counts = {}
    if cogs:
        for kind, table in (('cogSpec', cogs['CogData']), ('reserveCogSpec', cogs['ReserveCogData'])):
            for i, c in enumerate(table):
                parent = node_of(c['parentEntId'])
                n = parent.attachNewNode(f'{kind}{i}')
                n.setPos(*vec(c.get('pos'), (0, 0, 0)))
                n.setH(float(c.get('h', 0)))
                attrs = {k: v for k, v in c.items() if k not in ('pos', 'h', 'parentEntId')}
                w.add(i, c['parentEntId'], zone_of(c['parentEntId']), kind, mat12(n, model),
                      vec(c.get('pos'), (0, 0, 0)), [float(c.get('h', 0)), 0, 0], [1, 1, 1], attrs,
                      flags=tlv.FLAG_SYNTHETIC)
            counts[kind] = len(table)
        for cell_id, c in sorted(cogs['BattleCells'].items()):
            parent = node_of(c['parentEntId'])
            n = parent.attachNewNode(f'cell{cell_id}')
            n.setPos(*vec(c.get('pos'), (0, 0, 0)))
            w.add(cell_id, c['parentEntId'], zone_of(c['parentEntId']), 'battleCell', mat12(n, model),
                  vec(c.get('pos'), (0, 0, 0)), [0, 0, 0], [1, 1, 1], {}, flags=tlv.FLAG_SYNTHETIC)
        counts['battleCell'] = len(cogs['BattleCells'])
    report = w.write(out_path)
    report.update(counts)
    types = {}
    for spec in ents.values():
        types[spec['type']] = types.get(spec['type'], 0) + 1
    report['types'] = dict(sorted(types.items()))
    return report


def bake(level, args, base):
    import level_spec_load
    spec_name, cog_name = LEVELS[level]
    ents = level_spec_load.load_spec(spec_name)
    cogs = level_spec_load.load_cogs(cog_name) if cog_name else None
    mgr = next(e for e in ents.values() if e['type'] == 'levelMgr')
    model_path = Path(args.resources) / (mgr['modelFilename'] + '.bam')
    if not model_path.exists():
        # LawbotOfficeBoilerRoom_Battle00 names LB_Zone8a; the resources only have LB_Zone08a.
        import re
        padded = model_path.with_name(re.sub(r'(\D)(\d)([a-z]*\.bam)$', r'\g<1>0\2\3', model_path.name))
        if padded.exists():
            print(f'{level}: {model_path.name} missing, using {padded.name}', flush=True)
            model_path = padded
    model = base.loader.loadModel(str(model_path), noCache=True)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    report = {'level': level, 'spec': spec_name, 'model': mgr['modelFilename']}
    report['tlv'] = build_tlv(ents, cogs, model, out / f'{level}.tlv')
    (out / f'{level}.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2), flush=True)
    model.removeNode()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--level', default='all')
    ap.add_argument('--resources', default=str(default_resources()))
    ap.add_argument('--out', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data/level'))
    ap.add_argument('--tlv-only', action='store_true')
    args = ap.parse_args()
    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    LEVELS.update(room_levels())
    for level in (LEVELS if args.level == 'all' else args.level.split(',')):
        bake(level, args, base)
    base.destroy()


if __name__ == '__main__':
    main()
