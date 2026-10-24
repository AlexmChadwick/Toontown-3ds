#!/usr/bin/env python3
"""Bake the floors of the multi-room facilities: Cashbot mints, Lawbot DA offices (stages)
and Bossbot country clubs (docs/design/levels.md).

    .venv/bin/python -I tools/pipeline/bake_facility_floors.py [--only mint|stage|club] [--facility 12500]

A floor is a chain of level rooms joined by hallway connectors. Upstream builds it on the
client in DistributedMint/Stage/CountryClub.gotAllRooms from a layout that is deterministic
in (facility id, floor[, layout index]); this tool runs the stock upstream layout and room
classes (MintLayout + MintRoom.attachTo and friends) on the baked room models, so every
transform is exactly the one the upstream client computes.

Inputs: each room's level (bake_level.py --level <RoomName>: its .tz and .tlv, in the
room's own space). Outputs under sdmc-out/3ds/toontown3ds/data/level/:
  floors/<kind>_<facilityId>_<layout>_<floor>.tlf  one floor (format below)
  props/<connector>.ttm, props/door_frame_<style>.ttm  hallway connectors and door frames

TLF1 (little endian):
  header  4s magic 'TLF1', u16 version, u16 kind (0 mint, 1 stage, 2 club), u32 facilityId,
          u16 layoutIndex, u16 floor, u16 numPieces, u16 numDoorFrames, u32 numFloorTris,
          u32 piecesOffset, u32 doorFramesOffset, u32 floorTrisOffset, u32 collisionOffset,
          u32 collisionSize, u32 fileSize
  piece   (allRooms order: room 0, hallway 0, room 1, ... so the index is the roomNum)
          char name[48] (room level name, or the connector prop name), u16 roomNum,
          u8 isHallway, u8 pad, i16 roomId (-1 for hallways), u16 pad,
          f32 mat[12] (rows of the 3x4 piece -> floor transform: x axis, y axis, z axis, pos),
          f32 boundsMin[3], f32 boundsMax[3] (floor space)
  door    u16 roomNum, u16 style (0 Cashbot DoorFrame, 1 Lawbot DoorFrame), f32 mat[12]
  floorTri u16 roomNum, u16 pad, f32 v[9]  (floor space; the floor collision of each piece,
          like the mintFloorColl<roomNum> nodes of initFloorCollisions)
  collision  one TTC1 blob (collide.py) of the whole assembled floor, in floor space
Deterministic; run with -I.
"""
import argparse
import builtins
import json
import os
from pathlib import Path
import struct
import sys
import types
sys.dont_write_bytecode = True

os.environ.setdefault('TT_ETC1_THREADS', '3')
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
ROOT = HERE.parent.parent

MAGIC = b'TLF1'
VERSION = 1
HEADER = struct.Struct('<4sHHIHHHHIIIIIII')
PIECE = struct.Struct('<48sHBBhH12f3f3f')
DOOR = struct.Struct('<HH12f')
FLOOR_TRI = struct.Struct('<HH9f')

KIND_MINT, KIND_STAGE, KIND_CLUB = 0, 1, 2
KIND_NAMES = {KIND_MINT: 'mint', KIND_STAGE: 'stage', KIND_CLUB: 'club'}

# MintRoom.CashbotMintDoorFrame / StageRoom.CashbotStageDoorFrame / CountryClubRoom.
DOOR_FRAME_STYLES = {'phase_10/models/cashbotHQ/DoorFrame': 0}


def import_upstream():
    """Make the stock upstream layout and room modules importable. They only need the
    builtins ShowBase normally provides and the two compiled Panda game libraries, which
    the layout code never calls."""
    import level_spec_load
    sys.path.insert(0, str(level_spec_load.UPSTREAM))
    builtins.__execWarnings__ = 0
    builtins.__dev__ = 0
    for name in ('panda3d.otp', 'panda3d.toontown'):
        sys.modules.setdefault(name, types.ModuleType(name))


def ai_table(module, name, globals_module):
    """A module-level table of an AI manager (the AI modules need a running AI to import)."""
    import ast
    import level_spec_load
    tree = ast.parse((level_spec_load.COGHQ / f'{module}.py').read_text())
    node = next(n for n in tree.body if isinstance(n, ast.Assign) and getattr(n.targets[0], 'id', '') == name)
    return eval(compile(ast.Expression(node.value), module, 'eval'), {'ToontownGlobals': globals_module})


def floors(only=None, facility=None):
    """Every (kind, facilityId, layoutIndex, floor) the AI can create.
    Mints: MintManagerAI picks floor in range(MintNumFloors[id]).
    Stages: LawOfficeManagerAI.StageId2Layouts, floors of StageLayout.getNumFloors(layout).
    Clubs: CountryClubManagerAI.CountryClubId2Layouts, floors of countryClubLayouts[layout]."""
    from toontown.toonbase import ToontownGlobals
    from toontown.coghq import StageLayout, CountryClubLayout
    stage_layouts = ai_table('LawOfficeManagerAI', 'StageId2Layouts', ToontownGlobals)
    club_layouts = ai_table('CountryClubManagerAI', 'CountryClubId2Layouts', ToontownGlobals)
    out = []
    for mint_id, num in sorted(ToontownGlobals.MintNumFloors.items()):
        out += [(KIND_MINT, mint_id, 0, f) for f in range(num)]
    for stage_id, layouts in sorted(stage_layouts.items()):
        for layout in layouts:
            out += [(KIND_STAGE, stage_id, layout, f) for f in range(StageLayout.getNumFloors(layout))]
    for club_id, layouts in sorted(club_layouts.items()):
        for layout in layouts:
            out += [(KIND_CLUB, club_id, layout, f) for f in range(len(CountryClubLayout.countryClubLayouts[layout]))]
    return [k for k in out if (only is None or KIND_NAMES[k[0]] == only) and (facility is None or k[1] == facility)]


def layout_and_rooms(kind, facility_id, layout_index, floor):
    """(layout, room class, room id -> room level name) as the client's setFloorNum builds it."""
    from toontown.coghq import MintLayout, MintRoom, MintRoomSpecs
    from toontown.coghq import StageLayout, StageRoom, StageRoomSpecs
    from toontown.coghq import CountryClubLayout, CountryClubRoom, CountryClubRoomSpecs
    if kind == KIND_MINT:
        return (MintLayout.MintLayout(facility_id, floor), MintRoom.MintRoom,
                MintRoomSpecs.CashbotMintRoomId2RoomName)
    if kind == KIND_STAGE:
        return (StageLayout.StageLayout(facility_id, floor, layout_index), StageRoom.StageRoom,
                StageRoomSpecs.CashbotStageRoomId2RoomName)
    return (CountryClubLayout.CountryClubLayout(facility_id, floor, layout_index),
            CountryClubRoom.CountryClubRoom, CountryClubRoomSpecs.BossbotCountryClubRoomId2RoomName)


def prop_name(model_path):
    return Path(model_path).name.lower()


class Rooms:
    """Prepared room models (bake_level.prepare_level), loaded once and copied per floor."""
    def __init__(self, base, args):
        self.base = base
        self.args = args
        self.cache = {}

    def get(self, level):
        if level not in self.cache:
            import bake_level
            out = Path(self.args.out)
            model, _, _ = bake_level.prepare_level(level, self.args, self.base, out, write=False)
            model.detachNode()
            self.cache[level] = model
        return self.cache[level].copyTo(self.base.render)


def mat12(np_, other):
    m = np_.getMat(other)
    return [m.getCell(r, c) for r in range(4) for c in range(3)]


def floor_tris(geom, root):
    import collide
    mesh = collide.extract(geom, fallback_floor=False)
    root_inv = root.getNetTransform().getMat()
    root_inv.invertInPlace()
    out = []
    for a, b, c, _n, flags in ((t[0], t[1], t[2], t[3], t[4]) for t in mesh.tris):
        if flags & collide.FLOOR and not flags & collide.TRIGGER:
            out.append([tuple(root_inv.xformPoint(tuple(map(float, p)))) for p in (a, b, c)])
    return out


def bake_floor(key, rooms, base, args):
    from panda3d.core import NodePath
    import collide
    kind, facility_id, layout_index, floor = key
    layout, room_class, room_names = layout_and_rooms(*key)
    root = base.render.attachNewNode('floor')  # DistributedMint.gotAllRooms: self.geom

    # Door frames are loaded by attachTo through the builtin loader; record where they land.
    frames = []
    real_load = base.loader.loadModel

    def load_model(path, *a, **k):
        np_ = real_load(path, *a, **k)
        if str(path) in DOOR_FRAME_STYLES:
            frames.append((np_, DOOR_FRAME_STYLES[str(path)]))
        return np_
    builtins.loader = types.SimpleNamespace(loadModel=load_model)

    rng = layout.getRng()
    num_rooms = layout.getNumRooms()
    pieces = []  # (room object, name, roomId or -1)
    hallways = []
    for i, room_id in enumerate(layout.getRoomIds()):
        name = room_names[room_id]
        room = room_class()
        room.setGeom(rooms.get(name))
        room.setRoomNum(i * 2)
        if i == 0:
            room.getGeom().reparentTo(root)
        else:
            room.attachTo(hallways[i - 1], rng)
        pieces.append((room, name, room_id))
        if i < num_rooms - 1:
            path = layout.getHallwayModel(i)
            hallway = room_class()
            hallway.setGeom(real_load(path))
            hallway.attachTo(room, rng)
            hallway.setRoomNum(i * 2 + 1)
            hallways.append(hallway)
            pieces.append((hallway, prop_name(path), -1))
    builtins.loader = base.loader

    records = []
    tris = []
    for num, (piece, name, room_id) in enumerate(pieces):
        geom = piece.getGeom()
        lo, hi = geom.getTightBounds(root)
        records.append(PIECE.pack(name.encode()[:47], num, int(room_id < 0), 0, room_id, 0,
                                  *mat12(geom, root), *lo, *hi))
        tris += [(num, t) for t in floor_tris(geom, root)]
    doors = []
    for np_, style in frames:
        owner = next(n for n, (p, _, _) in enumerate(pieces) if p.getGeom().isAncestorOf(np_))
        doors.append(DOOR.pack(owner, style, *mat12(np_, root)))

    collision, stats = collide.extract(root, fallback_floor=False).pack()
    pieces_off = HEADER.size
    doors_off = pieces_off + PIECE.size * len(records)
    tris_off = doors_off + DOOR.size * len(doors)
    coll_off = (tris_off + FLOOR_TRI.size * len(tris) + 15) & ~15
    size = coll_off + len(collision)
    data = bytearray(HEADER.pack(MAGIC, VERSION, kind, facility_id, layout_index, floor, len(records),
                                 len(doors), len(tris), pieces_off, doors_off, tris_off, coll_off,
                                 len(collision), size))
    for r in records:
        data += r
    for d in doors:
        data += d
    for num, t in tris:
        data += FLOOR_TRI.pack(num, 0, *t[0], *t[1], *t[2])
    data += b'\0' * (coll_off - len(data))
    data += collision
    assert len(data) == size
    out = Path(args.out) / 'floors' / f'{KIND_NAMES[kind]}_{facility_id}_{layout_index}_{floor}.tlf'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(bytes(data))
    root.removeNode()
    return {'file': out.name, 'pieces': [p[1] for p in pieces], 'door_frames': len(doors),
            'floor_tris': len(tris), 'collision_bytes': len(collision), 'bytes': size}


def bake_connectors(base, args, keys):
    """The hallway connectors and door frames as props (in their own model space)."""
    import bake_level_props
    out = Path(args.out) / 'props'
    out.mkdir(parents=True, exist_ok=True)
    paths = set(DOOR_FRAME_STYLES)
    for key in keys:
        layout = layout_and_rooms(*key)[0]
        paths.update(layout.getHallwayModel(i) for i in range(layout.getNumRooms() - 1))
    report = {}
    for path in sorted(paths):
        model = base.loader.loadModel(path)
        name = 'door_frame_%d' % DOOR_FRAME_STYLES[path] if path in DOOR_FRAME_STYLES else prop_name(path)
        report[name] = bake_level_props.write_part(model, None, out / f'{name}.ttm')
        model.removeNode()
    return report


def main():
    import bake_level
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', choices=sorted(KIND_NAMES.values()))
    ap.add_argument('--facility', type=int)
    ap.add_argument('--resources', default=str(bake_level.default_resources()))
    ap.add_argument('--out', default=str(ROOT / 'sdmc-out/3ds/toontown3ds/data/level'))
    args = ap.parse_args()
    from panda3d.core import loadPrcFileData, getModelPath
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                    'default-model-extension .bam\n')
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    getModelPath().prependDirectory(args.resources)
    import_upstream()
    bake_level.LEVELS.update(bake_level.room_levels())
    keys = floors(args.only, args.facility)
    rooms = Rooms(base, args)
    report = {'connectors': bake_connectors(base, args, keys), 'floors': []}
    for key in keys:
        r = bake_floor(key, rooms, base, args)
        print(r['file'], len(r['pieces']), 'pieces', r['bytes'], 'bytes', flush=True)
        report['floors'].append(r)
    (Path(args.out) / 'floors' / 'manifest.json').write_text(json.dumps(report, indent=2, default=str) + '\n')
    base.destroy()


if __name__ == '__main__':
    main()
