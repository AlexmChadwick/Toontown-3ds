"""TLV1: baked level spec (factories, mints, DA offices, country clubs). Little endian.

The upstream level specs (toontown/coghq/*Spec.py, *Cogs.py) are Python dicts; the runtime
never evaluates them. bake_level.py flattens one spec into this table and the C++ reader is
source/world/level_spec.{h,cpp}.

Header (32): <4sHHIIIIII: magic TLV1, version 1, entity count, attr offset, attr count,
 float pool offset, float count, string pool offset, string pool bytes.
Entity (128): <IIHH24s12f3f3f3fIH2x
  entId, parentEntId, zoneNum (owning zone entity, 0 = UberZone), flags,
  type (NUL-terminated spec 'type', e.g. 'door'),
  world[12]: 3x4 row-major transform entity -> level model root (Panda convention: row i is
             basis vector i, last row the translation: p' = p.x*r0 + p.y*r1 + p.z*r2 + r3),
  pos[3], hpr[3], scale[3]: the local spec values (defaults 0 / 0 / 1),
  first attribute index, attribute count.
Attr (12): <IBBHI: name offset (string pool), kind, pad, count, data offset.
  kind 1 number  -> count floats at float pool [data]       (ints/bools are exact floats)
  kind 2 vector  -> count floats (Point3/Vec3/Vec4 components)
  kind 3 string  -> count bytes at string pool [data] (NUL-terminated as well)
  kind 4 list    -> count floats (a flat list of numbers; nested vectors are flattened)
  kind 5 none    -> count 0
Entities are sorted by entId; attrs of one entity are sorted by name. Synthetic entities
use type names that never occur in specs: 'cogSpec' / 'reserveCogSpec' (entId = index into
CogData / ReserveCogData, parent = the spec parentEntId, world = the Cog's spawn pose) and
'battleCell' (entId = battle cell id). Their entIds live in their own namespace, flagged by
FLAG_SYNTHETIC; lookups by entId skip them.
"""
import struct
from pathlib import Path

HEADER = struct.Struct('<4sHHIIIIII')
ENTITY = struct.Struct('<IIHH24s12f3f3f3fIH2x')
ATTR = struct.Struct('<IBBHI')
FLAG_SYNTHETIC = 1
NUMBER, VECTOR, STRING, LIST, NONE = 1, 2, 3, 4, 5
assert HEADER.size == 32 and ENTITY.size == 128 and ATTR.size == 12


class Writer:
    def __init__(self):
        self.entities = []

    def add(self, ent_id, parent, zone, etype, world, pos, hpr, scale, attrs, flags=0):
        self.entities.append((ent_id, parent, zone, flags, etype, world, pos, hpr, scale, attrs))

    def write(self, path):
        strings = bytearray()
        string_index = {}
        floats = []
        attrs = []

        def intern(s):
            if s not in string_index:
                string_index[s] = len(strings)
                strings.extend(s.encode('utf-8') + b'\0')
            return string_index[s]

        records = []
        self.entities.sort(key=lambda e: (e[3] & FLAG_SYNTHETIC, e[4] if e[3] & FLAG_SYNTHETIC else '', e[0]))
        for ent_id, parent, zone, flags, etype, world, pos, hpr, scale, values in self.entities:
            first = len(attrs)
            for name in sorted(values):
                kind, count, data = encode(values[name], floats, intern)
                if kind is None:
                    continue
                attrs.append((intern(name), kind, count, data))
            name = etype.encode('ascii')
            if len(name) >= 24:
                raise ValueError(f'entity type too long: {etype}')
            records.append(ENTITY.pack(ent_id, parent, zone, flags, name.ljust(24, b'\0'), *world, *pos, *hpr,
                                       *scale, first, len(attrs) - first))
        ao = HEADER.size + ENTITY.size * len(records)
        fo = ao + ATTR.size * len(attrs)
        so = fo + 4 * len(floats)
        out = bytearray(HEADER.pack(b'TLV1', 1, len(records), ao, len(attrs), fo, len(floats), so, len(strings)))
        for r in records:
            out += r
        for a in attrs:
            out += ATTR.pack(a[0], a[1], 0, a[2], a[3])
        out += struct.pack(f'<{len(floats)}f', *floats)
        out += strings
        Path(path).write_bytes(out)
        return {'entities': len(records), 'attrs': len(attrs), 'floats': len(floats), 'string_bytes': len(strings),
                'bytes': len(out)}


def flatten(v):
    if isinstance(v, (list, tuple)):
        out = []
        for x in v:
            out.extend(flatten(x))
        return out
    if hasattr(v, '__len__') and hasattr(v, '__getitem__') and not isinstance(v, (str, dict)):
        return [float(v[i]) for i in range(len(v))]
    if isinstance(v, (bool, int, float)):
        return [float(v)]
    raise TypeError(f'unsupported list element {v!r}')


def encode(v, floats, intern):
    if v is None:
        return NONE, 0, 0
    if isinstance(v, str):
        b = v.encode('utf-8')
        if len(b) > 0xFFFF:
            raise ValueError('string too long')
        return STRING, len(b), intern(v)
    if isinstance(v, (bool, int, float)):
        floats.append(float(v))
        return NUMBER, 1, len(floats) - 1
    if isinstance(v, (list, tuple)):
        try:
            flat = flatten(v)
        except TypeError:
            return None, 0, 0  # e.g. lists of strings or dicts: not needed at runtime
        start = len(floats)
        floats.extend(flat)
        return LIST, len(flat), start
    if isinstance(v, dict):
        return None, 0, 0  # e.g. country-club 'cogDict' tables stay offline
    if hasattr(v, '__len__') and hasattr(v, '__getitem__'):
        start = len(floats)
        floats.extend(float(v[i]) for i in range(len(v)))
        return VECTOR, len(v), start
    return None, 0, 0  # dicts and other structures stay offline
