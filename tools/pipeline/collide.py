"""Collision extraction from a Panda scene + writer for the TTC runtime collision format.

``extract(nodepath)`` walks every (unstashed) CollisionNode under ``nodepath``, moves its
solids into world space (net transform) and classifies them by into-collide-mask, the
same way the game's traversers see them (otp/otpbase/OTPGlobals.py:5-8):

    WallBitmask   = 1  -> WALL    (GravityWalker wall sphere, CollisionHandlerPusher)
    FloorBitmask  = 2  -> FLOOR   (GravityWalker down ray, CollisionHandlerGravity)
    CameraBitmask = 4  -> CAMERA  (LocalAvatar ccLine segment / ccSphere pusher)

A solid can be in several classes (TTC fences/hedges/bridge rails are 0x5 = wall +
camera); it is stored once and referenced from each class list. Names are not used
for classification because every TTC/DNA model sets explicit masks; names such as
``coll_*floor*`` agree with the masks anyway.

TRIGGER volumes: solids that are intangible (``isTangible() == False``, e.g. the door
``*_trigger`` polygons in phase_3.5/models/modules/doors_practical.bam, mask 0x1) or
tangible solids with none of the three bits above (FloorEventBitmask 16 etc.) are not
collision. They are kept as named trigger volumes (node name + owner name + mask) for
later gameplay code (doors, fishing spots, trolley...). GhostBitmask (2048) and
CameraTransparentBitmask (8) only matter for those bits, so a solid carrying only them
becomes a trigger with that mask.

No-collision fallback: if the whole scene has no FLOOR solid, the visible geometry
(GeomNodes, hidden ones skipped) contributes every triangle whose world normal has
z >= 0.707 (GravityWalker standableGround) as FLOOR, so a bare terrain model is still
walkable. Props without collision get nothing (they are walk-through in the game too).
``fallback_floor`` can force it on/off.

Solid conversion:
  CollisionPolygon  -> fan-triangulated triangles (convex in Panda)
  CollisionBox      -> 12 triangles
  CollisionFloorMesh-> its triangles
  CollisionSphere   -> capsule with a == b
  CollisionCapsule  -> capsule (a, b, r); non-uniform scale: r scaled by the mean of the
                       two axes perpendicular to the capsule (approximation)
  CollisionPlane    -> infinite plane (unbucketed, tested by every query)
  others (InvSphere, Ray, Line, Segment, Parabola) -> skipped with a warning

Format TTC v1 (little endian; every section starts 16-byte aligned):

    Header (96 bytes)
      char[4] magic "TTC1"; u16 version (1); u16 flags (0)
      f32 gridOrigin[2]; f32 cellSize; u16 gridW; u16 gridH
      u32 numTris; u32 numCaps; u32 numPlanes; u32 numTriggers
      f32 boundsMin[3]; f32 boundsMax[3]        -- all non-trigger solids
      u32 triOffset; u32 capOffset; u32 planeOffset; u32 cellTableOffset
      u32 cellDataOffset; u32 cellDataCount (u16 units); u32 triggerOffset; u32 fileSize
    Tri (56 bytes): f32 v0[3], v1[3], v2[3]; f32 n[3] (unit); f32 d (= n.v0); u32 flags
    Cap (32 bytes): f32 a[3], b[3], r; u32 flags            (sphere: a == b)
    Plane (20 bytes): f32 n[3], d (n.p = d on the plane, solid side n.p < d); u32 flags
    Cell table: u32[gridW * gridH], row-major (cell = cy * gridW + cx), each an offset
      in u16 units into cell data. Every empty cell points at offset 0 (a zero header).
    Cell data: u16 stream; at each cell offset
      u16 counts[6] = floorTris, floorCaps, wallTris, wallCaps, camTris, camCaps
      followed by that many u16 indices (into the tri / cap tables) in the same order.
    Trigger (100 bytes): u8 kind (0 = caps, 1 = tris); u8 pad; u16 count; u32 first;
      u32 intoMask; f32 min[3], max[3]; char name[32]; char owner[32] (NUL padded)
      Trigger geometry lives in the tri / cap tables with flag kTrigger and is not in
      any cell list.

flags: bit0 WALL, bit1 FLOOR, bit2 CAMERA (copied from the into mask), bit7 TRIGGER,
bit8 FALLBACK (floor made from visible geometry).

Cell (x, y) covers [origin + c * cellSize, origin + (c + 1) * cellSize). A tri is put in
every cell its XY projection overlaps (exact 2D SAT test, 0.01 margin); a cap in every
cell its XY bounding box touches. Runtime queries never need more than the cells their
own XY footprint covers.

Why float and not int16: Old 3DS has 24 MB of regular heap; TTC terrain is ~560 tris
(31 KB) and a fully assembled zone a few thousand (< 0.5 MB). The ARM11 VFP does float
math natively, so dequantizing int16 on every test would cost more than it saves.

Must stay in sync with source/world/collision.cpp.
"""
import math
import struct
from collections import Counter

import numpy as np

MAGIC = b'TTC1'
VERSION = 1
HEADER_SIZE = 96
TRI_SIZE = 56
CAP_SIZE = 32
PLANE_SIZE = 20
TRIGGER_SIZE = 100

WALL, FLOOR, CAMERA = 1, 2, 4
SOLID_MASK = WALL | FLOOR | CAMERA
TRIGGER = 0x80
FALLBACK = 0x100

STANDABLE_GROUND = 0.707  # direct/controls/GravityWalker.py:38 standableGround
DEFAULT_CELL = 8.0
MIN_AREA = 1e-6


def _align(n, a=16):
    return (n + a - 1) // a * a


class Trigger:
    def __init__(self, kind, first, count, mask, lo, hi, name, owner):
        self.kind, self.first, self.count, self.mask = kind, first, count, mask
        self.lo, self.hi, self.name, self.owner = lo, hi, name, owner


class CollisionMesh:
    """World-space collision soup. tris: list of (v0, v1, v2, flags); caps: (a, b, r, flags);
    planes: (n, d, flags); triggers: Trigger."""

    def __init__(self):
        self.tris = []
        self.caps = []
        self.planes = []
        self.triggers = []
        self.warnings = []
        self.skipped = Counter()

    # -- building -----------------------------------------------------------------
    def add_tri(self, v0, v1, v2, flags, normal_hint=None):
        v0, v1, v2 = (np.asarray(v, dtype=np.float64) for v in (v0, v1, v2))
        n = np.cross(v1 - v0, v2 - v0)
        area2 = float(np.linalg.norm(n))
        if area2 < 2 * MIN_AREA:
            return False
        n /= area2
        if normal_hint is not None and float(np.dot(n, normal_hint)) < 0:
            v1, v2, n = v2, v1, -n
        self.tris.append((v0, v1, v2, n, int(flags)))
        return True

    def add_cap(self, a, b, r, flags):
        self.caps.append((np.asarray(a, dtype=np.float64), np.asarray(b, dtype=np.float64),
                          float(r), int(flags)))

    def counts(self):
        c = Counter()
        for *_, f in self.tris:
            for bit, name in ((WALL, 'wall'), (FLOOR, 'floor'), (CAMERA, 'camera')):
                if f & bit and not f & TRIGGER:
                    c[name + '_tris'] += 1
            if f & TRIGGER:
                c['trigger_tris'] += 1
            if f & FALLBACK:
                c['fallback_floor_tris'] += 1
        for *_, f in self.caps:
            for bit, name in ((WALL, 'wall'), (FLOOR, 'floor'), (CAMERA, 'camera')):
                if f & bit and not f & TRIGGER:
                    c[name + '_caps'] += 1
            if f & TRIGGER:
                c['trigger_caps'] += 1
        return dict(sorted(c.items()))

    # -- writing ------------------------------------------------------------------
    def write(self, path, cell_size=DEFAULT_CELL, max_bytes=None):
        data, stats = self.pack(cell_size)
        if max_bytes is not None and len(data) > max_bytes:
            raise RuntimeError(f'{path}: {len(data)} bytes exceeds budget {max_bytes}')
        with open(path, 'wb') as f:
            f.write(data)
        return stats

    def pack(self, cell_size=DEFAULT_CELL):
        solid_tris = [i for i, t in enumerate(self.tris) if not t[4] & TRIGGER]
        solid_caps = [i for i, c in enumerate(self.caps) if not c[3] & TRIGGER]
        if len(self.tris) > 65535 or len(self.caps) > 65535:
            raise RuntimeError('too many collision solids for u16 indices')

        pts = [v for i in solid_tris for v in self.tris[i][:3]]
        for i in solid_caps:
            a, b, r, _ = self.caps[i]
            pts += [a - r, a + r, b - r, b + r]
        if pts:
            pts = np.array(pts)
            lo, hi = pts.min(axis=0), pts.max(axis=0)
        else:
            lo = hi = np.zeros(3)
        ox = math.floor(lo[0] / cell_size) * cell_size
        oy = math.floor(lo[1] / cell_size) * cell_size
        gw = max(1, int(math.ceil((hi[0] - ox) / cell_size + 1e-9)))
        gh = max(1, int(math.ceil((hi[1] - oy) / cell_size + 1e-9)))
        if gw > 65535 or gh > 65535:
            raise RuntimeError('grid too large')

        # cells[c] = [floorT, floorC, wallT, wallC, camT, camC]
        cells = {}

        def cell_list(cx, cy):
            k = cy * gw + cx
            if k not in cells:
                cells[k] = [[], [], [], [], [], []]
            return cells[k]

        margin = 0.01
        for i in solid_tris:
            v0, v1, v2, _, f = self.tris[i]
            p = np.array([v0[:2], v1[:2], v2[:2]])
            for cx, cy in _tri_cells(p, ox, oy, cell_size, gw, gh, margin):
                lst = cell_list(cx, cy)
                if f & FLOOR:
                    lst[0].append(i)
                if f & WALL:
                    lst[2].append(i)
                if f & CAMERA:
                    lst[4].append(i)
        for i in solid_caps:
            a, b, r, f = self.caps[i]
            x0 = min(a[0], b[0]) - r - margin
            x1 = max(a[0], b[0]) + r + margin
            y0 = min(a[1], b[1]) - r - margin
            y1 = max(a[1], b[1]) + r + margin
            for cy in range(_clampi((y0 - oy) / cell_size, gh), _clampi((y1 - oy) / cell_size, gh) + 1):
                for cx in range(_clampi((x0 - ox) / cell_size, gw), _clampi((x1 - ox) / cell_size, gw) + 1):
                    lst = cell_list(cx, cy)
                    if f & FLOOR:
                        lst[1].append(i)
                    if f & WALL:
                        lst[3].append(i)
                    if f & CAMERA:
                        lst[5].append(i)

        cell_data = [0] * 6  # shared empty header at offset 0
        cell_table = [0] * (gw * gh)
        max_cell = Counter()
        for k in sorted(cells):
            lists = cells[k]
            cell_table[k] = len(cell_data)
            cell_data += [len(lst) for lst in lists]
            for lst in lists:
                cell_data += lst
            for name, lst in zip(('floor_tris', 'floor_caps', 'wall_tris', 'wall_caps',
                                  'cam_tris', 'cam_caps'), lists):
                max_cell[name] = max(max_cell[name], len(lst))

        # -- serialize --
        tri_off = HEADER_SIZE
        cap_off = _align(tri_off + TRI_SIZE * len(self.tris))
        plane_off = _align(cap_off + CAP_SIZE * len(self.caps))
        table_off = _align(plane_off + PLANE_SIZE * len(self.planes))
        data_off = _align(table_off + 4 * len(cell_table))
        trig_off = _align(data_off + 2 * len(cell_data))
        size = _align(trig_off + TRIGGER_SIZE * len(self.triggers))

        out = bytearray(size)
        struct.pack_into('<4sHH2ffHHIIII3f3fIIIIIIII', out, 0, MAGIC, VERSION, 0,
                         ox, oy, cell_size, gw, gh,
                         len(self.tris), len(self.caps), len(self.planes), len(self.triggers),
                         *lo, *hi,
                         tri_off, cap_off, plane_off, table_off, data_off, len(cell_data),
                         trig_off, size)
        for j, (v0, v1, v2, n, f) in enumerate(self.tris):
            struct.pack_into('<13fI', out, tri_off + j * TRI_SIZE, *v0, *v1, *v2, *n,
                             float(np.dot(n, v0)), f)
        for j, (a, b, r, f) in enumerate(self.caps):
            struct.pack_into('<7fI', out, cap_off + j * CAP_SIZE, *a, *b, r, f)
        for j, (n, d, f) in enumerate(self.planes):
            struct.pack_into('<4fI', out, plane_off + j * PLANE_SIZE, *n, d, f)
        struct.pack_into(f'<{len(cell_table)}I', out, table_off, *cell_table)
        struct.pack_into(f'<{len(cell_data)}H', out, data_off, *cell_data)
        for j, t in enumerate(self.triggers):
            struct.pack_into('<BBHII3f3f32s32s', out, trig_off + j * TRIGGER_SIZE,
                             t.kind, 0, t.count, t.first, t.mask, *t.lo, *t.hi,
                             _cstr(t.name), _cstr(t.owner))

        stats = {
            'bytes': size,
            'tris': len(self.tris), 'caps': len(self.caps), 'planes': len(self.planes),
            'triggers': len(self.triggers),
            'grid': [gw, gh], 'cell_size': cell_size, 'origin': [ox, oy],
            'occupied_cells': len(cells),
            'cell_index_entries': len(cell_data) - 6 - 6 * len(cells),
            'max_per_cell': dict(sorted(max_cell.items())),
            'bounds': [list(map(float, lo)), list(map(float, hi))],
            'classes': self.counts(),
            'trigger_names': sorted(Counter(t.name for t in self.triggers).items()),
            'skipped': dict(sorted(self.skipped.items())),
            'warnings': self.warnings[:20],
        }
        return bytes(out), stats


def _cstr(s):
    b = s.encode('ascii', 'replace')[:31]
    return b + b'\0' * (32 - len(b))


def _clampi(v, n):
    return min(max(int(math.floor(v)), 0), n - 1)


def _tri_cells(p, ox, oy, cs, gw, gh, margin):
    """Cells whose (margin-expanded) rectangle overlaps the 2D triangle p (3x2)."""
    x0, y0 = p.min(axis=0) - margin
    x1, y1 = p.max(axis=0) + margin
    cx0, cx1 = _clampi((x0 - ox) / cs, gw), _clampi((x1 - ox) / cs, gw)
    cy0, cy1 = _clampi((y0 - oy) / cs, gh), _clampi((y1 - oy) / cs, gh)
    axes = []
    for k in range(3):
        e = p[(k + 1) % 3] - p[k]
        ax = np.array([-e[1], e[0]])
        ln = float(np.linalg.norm(ax))
        if ln > 1e-9:
            axes.append(ax / ln)
    tri_proj = [(float((p @ ax).min()), float((p @ ax).max())) for ax in axes]
    for cy in range(cy0, cy1 + 1):
        for cx in range(cx0, cx1 + 1):
            rx0 = ox + cx * cs - margin
            ry0 = oy + cy * cs - margin
            rx1 = rx0 + cs + 2 * margin
            ry1 = ry0 + cs + 2 * margin
            corners = np.array([[rx0, ry0], [rx1, ry0], [rx0, ry1], [rx1, ry1]])
            ok = True
            for ax, (tmin, tmax) in zip(axes, tri_proj):
                pr = corners @ ax
                if pr.max() < tmin or pr.min() > tmax:
                    ok = False
                    break
            if ok:
                yield cx, cy


# -- extraction ------------------------------------------------------------------------

def _owner_name(cnp, root):
    """Closest ancestor that names a DNA object (has ':' or 'DNARoot'), else the parent."""
    np_ = cnp.getParent()
    parent = np_.getName() if not np_.isEmpty() else ''
    while not np_.isEmpty() and np_ != root:
        nm = np_.getName()
        if ':' in nm or 'DNARoot' in nm:
            return nm
        np_ = np_.getParent()
    return parent


def _pts(mat, points):
    return [np.array(mat.xformPoint(p), dtype=np.float64) for p in points]


def _add_solid(mesh, solid, mat, flags, warn_name):
    """Append one solid's geometry. Returns ('tris'|'caps', first, count) or None."""
    from panda3d.core import (CollisionBox, CollisionCapsule, CollisionPlane,
                              CollisionPolygon, CollisionSphere)
    try:
        from panda3d.core import CollisionFloorMesh
    except ImportError:  # pragma: no cover
        CollisionFloorMesh = None

    first_t, first_c = len(mesh.tris), len(mesh.caps)
    if isinstance(solid, CollisionPolygon):
        pts = _pts(mat, solid.getPoints())
        hint = np.array(mat.xformVec(solid.getNormal()), dtype=np.float64)
        for k in range(1, len(pts) - 1):
            mesh.add_tri(pts[0], pts[k], pts[k + 1], flags, hint)
    elif isinstance(solid, CollisionBox):
        lo, hi = solid.getMin(), solid.getMax()
        c = [np.array(mat.xformPoint((x, y, z)), dtype=np.float64)
             for z in (lo[2], hi[2]) for y in (lo[1], hi[1]) for x in (lo[0], hi[0])]
        centre = sum(c) / 8
        for a, b, cc, d in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6),
                            (0, 2, 6, 4), (1, 5, 7, 3)):
            face = (c[a] + c[b] + c[cc] + c[d]) / 4
            hint = face - centre
            mesh.add_tri(c[a], c[b], c[cc], flags, hint)
            mesh.add_tri(c[a], c[cc], c[d], flags, hint)
    elif CollisionFloorMesh is not None and isinstance(solid, CollisionFloorMesh):
        verts = [np.array(mat.xformPoint(solid.getVertex(i)), dtype=np.float64)
                 for i in range(solid.getNumVertices())]
        for i in range(solid.getNumTriangles()):
            t = solid.getTriangle(i)
            mesh.add_tri(verts[t[0]], verts[t[1]], verts[t[2]], flags, np.array([0, 0, 1.0]))
    elif isinstance(solid, (CollisionSphere, CollisionCapsule)):
        if isinstance(solid, CollisionCapsule):
            a, b = solid.getPointA(), solid.getPointB()
        else:
            a = b = solid.getCenter()
        wa, wb = (np.array(mat.xformPoint(p), dtype=np.float64) for p in (a, b))
        axis = np.array(b - a, dtype=np.float64)
        sx = [float(np.linalg.norm(np.array(mat.xformVec(v)))) for v in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
        if np.linalg.norm(axis) > 1e-6:
            axis = np.abs(axis / np.linalg.norm(axis))
            # weight each axis scale by how perpendicular it is to the capsule axis
            w = 1.0 - axis ** 2
            scale = float(np.dot(w, sx) / max(w.sum(), 1e-9))
        else:
            scale = sum(sx) / 3
        if max(sx) - min(sx) > 1e-3 * max(sx):
            mesh.warnings.append(f'{warn_name}: non-uniform scale on round solid, radius approximated')
        mesh.add_cap(wa, wb, solid.getRadius() * scale, flags)
    elif isinstance(solid, CollisionPlane):
        pl = solid.getPlane()
        n = np.array(mat.xformVec(pl.getNormal()), dtype=np.float64)
        p = np.array(mat.xformPoint(pl.getPoint()), dtype=np.float64)
        n /= np.linalg.norm(n)
        # Panda: solid side is behind the plane (n.p < d).
        mesh.planes.append((n, float(np.dot(n, p)), flags))
        return None
    else:
        mesh.skipped[type(solid).__name__] += 1
        return None
    if len(mesh.tris) > first_t:
        return ('tris', first_t, len(mesh.tris) - first_t)
    if len(mesh.caps) > first_c:
        return ('caps', first_c, len(mesh.caps) - first_c)
    return None


def _visible_floor(mesh, nodepath, standable):
    from panda3d.core import GeomVertexReader
    added = 0
    for gnp in nodepath.findAllMatches('**/+GeomNode'):
        if gnp.isHidden():
            continue
        mat = gnp.getNetTransform().getMat()
        gn = gnp.node()
        for gi in range(gn.getNumGeoms()):
            geom = gn.getGeom(gi).decompose()
            vdata = geom.getVertexData()
            reader = GeomVertexReader(vdata, 'vertex')
            verts = []
            while not reader.isAtEnd():
                verts.append(np.array(mat.xformPoint(reader.getData3()), dtype=np.float64))
            for pi in range(geom.getNumPrimitives()):
                prim = geom.getPrimitive(pi)
                for t in range(prim.getNumPrimitives()):
                    s = prim.getPrimitiveStart(t)
                    if prim.getPrimitiveEnd(t) - s != 3:
                        continue
                    v = [verts[prim.getVertex(s + k)] for k in range(3)]
                    n = np.cross(v[1] - v[0], v[2] - v[0])
                    ln = np.linalg.norm(n)
                    if ln < 2 * MIN_AREA or abs(n[2]) / ln < standable:
                        continue
                    if mesh.add_tri(v[0], v[1], v[2], FLOOR | FALLBACK, np.array([0, 0, 1.0])):
                        added += 1
    return added


def extract(nodepath, fallback_floor='auto', standable=STANDABLE_GROUND):
    """Collect collision geometry under ``nodepath`` (any NodePath: a loaded .bam or an
    assembled DNA zone) into a world-space CollisionMesh."""
    mesh = CollisionMesh()
    for cnp in nodepath.findAllMatches('**/+CollisionNode'):
        node = cnp.node()
        mask = node.getIntoCollideMask().getWord()
        if mask == 0:
            mesh.skipped['zero_mask_nodes'] += 1
            continue
        mat = cnp.getNetTransform().getMat()
        name = node.getName()
        owner = None
        for si in range(node.getNumSolids()):
            solid = node.getSolid(si)
            is_trigger = (not solid.isTangible()) or not (mask & SOLID_MASK)
            flags = TRIGGER if is_trigger else (mask & SOLID_MASK)
            res = _add_solid(mesh, solid, mat, flags, name)
            if res is None or not is_trigger:
                continue
            kind, first, count = res
            if kind == 'tris':
                p = np.array([v for t in mesh.tris[first:first + count] for v in t[:3]])
                lo, hi = p.min(axis=0), p.max(axis=0)
            else:
                lo = np.min([np.minimum(a, b) - r for a, b, r, _ in mesh.caps[first:first + count]], axis=0)
                hi = np.max([np.maximum(a, b) + r for a, b, r, _ in mesh.caps[first:first + count]], axis=0)
            if owner is None:
                owner = _owner_name(cnp, nodepath)
            mesh.triggers.append(Trigger(1 if kind == 'tris' else 0, first, count, mask,
                                         lo, hi, name, owner))
    has_floor = any(t[4] & FLOOR and not t[4] & TRIGGER for t in mesh.tris) or \
        any(c[3] & FLOOR and not c[3] & TRIGGER for c in mesh.caps) or \
        any(p[2] & FLOOR for p in mesh.planes)
    if fallback_floor is True or (fallback_floor == 'auto' and not has_floor):
        n = _visible_floor(mesh, nodepath, standable)
        mesh.warnings.append(f'no floor collision: {n} visible triangles used as floor')
    return mesh
