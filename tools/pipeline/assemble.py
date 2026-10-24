"""Assemble a parsed DNA tree into a Panda3D scene graph.

Re-implements the placement rules of the game's DNA loader (groups, props,
landmark and flat buildings, walls, windows, cornices, doors, sign boards and
sign graphics, streets) so the offline baker sees the same geometry the game
shows. Written from a reading of the placement semantics, not a code port.

Things deliberately kept from the source models:
  * CollisionNodes stay in the graph (a later collision baker reads them).
  * Hidden nodes (3D door panels, door holes) stay hidden; extract.py skips them.

Decals: the game marks wall/door/sign GeomNodes with DecalEffect and draws their
children on top. extract.py knows nothing about decals, so after assembly every
child of a decal base GeomNode is moved under a holder node named
``__decalN__`` (N = decal nesting depth). Feed :data:`LAYER_RULES` to
``extract.Extractor(layer_rules=...)`` and the runtime depth bias does the rest.

Top-level visgroups are tagged ``dna_visgroup=<name>`` for later chunking.
"""
import math
import random
from collections import Counter

from panda3d.core import (BillboardEffect, DecalEffect, GeomNode, GeomVertexReader, Mat4,
                          ModelNode, NodePath, PandaNode, Vec3)

MAX_DECAL_DEPTH = 4
DECAL_PUSH = 0.03   # units each decal level is lifted off its base (see lift_decals)
LAYER_RULES = [(f'__decal{n}__', n) for n in range(1, MAX_DECAL_DEPTH + 1)]
VISGROUP_TAG = 'dna_visgroup'

# Window placement (fractions of the wall, which is a unit square scaled by
# width x height). Index = window count.
_WINDOW_SLOTS = {
    1: [(0.5, 0.5)],
    2: [(1 / 3, 0.5), (2 / 3, 0.5)],
    3: [(0.33, 0.66), (0.5, 0.33), (0.66, 0.66)],
    4: [(0.33, 0.75), (0.66, 0.75), (0.33, 0.25), (0.66, 0.25)],
}
_POS_JITTER = 0.025
_HPR_JITTER = 6.0
_SCALE_JITTER = 0.025

# Property keywords: plain values, never traversed as children.
_PROPERTIES = {
    'code', 'pos', 'hpr', 'nhpr', 'scale', 'color', 'width', 'height', 'count', 'title',
    'article', 'building_type', 'vis', 'anim', 'cell_id', 'texture', 'kern', 'wiggle',
    'stumble', 'stomp', 'indent', 'flags', 'letters',
}
# Blocks with no geometry for us.
_IGNORED = {'suit_edge', 'store_suit_point', 'battle_cell'}

WHITE = (1.0, 1.0, 1.0, 1.0)


def _hpr(node):
    # 'nhpr' is the modern convention. Legacy 'hpr' would need the old->new HPR
    # conversion; for heading-only rotations (all TTC data) they are identical.
    c = node.find('nhpr')
    if c is None:
        c = node.find('hpr')
    if c is None or len(c.values) < 3:
        return (0.0, 0.0, 0.0)
    return tuple(float(v) for v in c.values[:3])


class Assembler:
    def __init__(self, storage, seed=1):
        self.storage = storage
        self.rng = random.Random(seed)
        self.unresolved = Counter()   # code -> uses that failed to resolve
        self.skipped = Counter()      # feature -> count
        self.wall_height = 0.0
        self.billboards_crossed = 0

    # -- helpers -----------------------------------------------------------
    def _find(self, code):
        np_ = self.storage.find_node(code) if code else None
        if np_ is None or np_.isEmpty():
            self.unresolved[code] += 1
            return None
        return np_

    def _jitter(self, amount):
        return self.rng.random() * amount - amount / 2.0

    def _children(self, dnode, parent):
        for c in dnode.children:
            self.traverse(c, parent)

    # -- dispatch ----------------------------------------------------------
    def traverse(self, dnode, parent):
        kind = dnode.kind
        if kind in _PROPERTIES or kind in _IGNORED:
            return None
        fn = getattr(self, '_t_' + kind, None)
        if fn is None:
            self.skipped[kind] += 1
            return None
        return fn(dnode, parent)

    def _t_root(self, d, parent):
        self._children(d, parent)
        return parent

    def _t_group(self, d, parent):
        np_ = parent.attachNewNode(PandaNode(d.name or 'group'))
        self._children(d, np_)
        return np_

    def _t_visgroup(self, d, parent):
        np_ = parent.attachNewNode(PandaNode(d.name or 'visgroup'))
        np_.setTag(VISGROUP_TAG, d.name or '')
        self._children(d, np_)
        return np_

    def _t_node(self, d, parent):
        np_ = parent.attachNewNode(PandaNode(d.name or 'node'))
        np_.setPosHprScale(d.vec('pos', (0, 0, 0)), _hpr(d), d.vec('scale', (1, 1, 1)))
        self._children(d, np_)
        return np_

    def _t_prop(self, d, parent):
        code = d.get('code', '')
        if code == 'DCS':
            # Empty placeholder node (NPC origins etc.).
            mn = ModelNode(d.name or 'DCS')
            mn.setPreserveTransform(ModelNode.PT_net)
            np_ = parent.attachNewNode(mn)
        else:
            src = self._find(code)
            if src is None:
                return None
            np_ = src.copyTo(parent)
            np_.setName(d.name or code)
        np_.setPosHprScale(d.vec('pos', (0, 0, 0)), _hpr(d), d.vec('scale', (1, 1, 1)))
        # A color *scale*, so vertex/poly colors already in the model survive.
        np_.setColorScale(d.vec('color', WHITE))
        if d.kind != 'prop':
            np_.setTag('DNAAnim', str(d.get('anim', '')))
        self._children(d, np_)
        return np_

    _t_anim_prop = _t_prop
    _t_interactive_prop = _t_prop

    def _t_street(self, d, parent):
        src = self._find(d.get('code', ''))
        if src is None:
            return None
        np_ = src.copyTo(parent)
        np_.setName(d.name or 'street')
        tex_codes = [c.values[0] for c in d.find_all('texture') if c.values]
        colors = [c for c in d.find_all('color')]
        for i, suffix in enumerate(('_street', '_sidewalk', '_curb')):
            part = np_.find('**/*' + suffix)
            if part.isEmpty():
                continue
            if i < len(tex_codes):
                tex = self.storage.find_texture(tex_codes[i])
                if tex is None:
                    self.unresolved['texture:' + tex_codes[i]] += 1
                else:
                    part.setTexture(tex, 1)
            if i < len(colors) and len(colors[i].values) >= 4:
                part.setColorScale(*[float(v) for v in colors[i].values[:4]])
        np_.setPosHprScale(d.vec('pos', (0, 0, 0)), _hpr(d), d.vec('scale', (1, 1, 1)))
        return np_

    # -- landmark buildings ------------------------------------------------
    def _t_landmark_building(self, d, parent):
        src = self._find(d.get('code', ''))
        if src is None:
            return None
        np_ = src.copyTo(parent)
        np_.setName(d.name or 'landmark')
        np_.setPosHprScale(d.vec('pos', (0, 0, 0)), _hpr(d), d.vec('scale', (1, 1, 1)))
        # Wall 'color' on landmarks is parsed but unused by the game.
        self._children(d, np_)
        if d.get('building_type', '') != 'hq':
            o = np_.find('**/*door_origin')
            if not o.isEmpty():
                o.removeNode()
        o = np_.find('**/*sign_origin')
        if not o.isEmpty():
            o.removeNode()
        return np_

    def _t_anim_building(self, d, parent):
        self.skipped['anim_building'] += 1
        return None

    @staticmethod
    def _decal_base(parent, patterns):
        """First GeomNode at/under the first match of patterns, made a decal base."""
        front = NodePath()
        for pat in patterns:
            front = parent.find(pat)
            if not front.isEmpty():
                break
        if front.isEmpty():
            return NodePath()
        if not front.node().isGeomNode():
            front = front.find('**/+GeomNode')
            if front.isEmpty():
                return NodePath()
        front.node().setEffect(DecalEffect.make())
        return front

    def _t_door(self, d, parent):
        front = self._decal_base(parent, ['**/*_front'])
        if front.isEmpty():
            front = parent
        src = self._find(d.get('code', ''))
        if src is None:
            return None
        origin = parent.find('**/*door_origin')
        if origin.isEmpty():
            self.skipped['door without door_origin'] += 1
            return None
        door = src.copyTo(front)
        door.setPosHprScale(origin, (0, 0, 0), (0, 0, 0), (1, 1, 1))
        color = d.vec('color', WHITE)
        door.setColor(color)
        self._setup_door_parts(door, parent, color)
        return door

    @staticmethod
    def _setup_door_parts(door, building, color):
        hole_l = door.find('door_*_hole_left')
        hole_r = door.find('door_*_hole_right')
        left = door.find('door_*_left')
        right = door.find('door_*_right')
        flat = door.find('door_*_flat')
        if not flat.isEmpty():
            # The holes sit on the frame as decals; the 3D swinging panels are
            # pulled out and hidden until a door opens.
            for h in (hole_l, hole_r):
                if not h.isEmpty():
                    h.wrtReparentTo(flat)
            flat.node().setEffect(DecalEffect.make())
            for p in (left, right):
                if not p.isEmpty():
                    p.wrtReparentTo(building)
                    p.hide()
        for h in (hole_l, hole_r):
            if not h.isEmpty():
                h.hide()
                h.setColor(0, 0, 0, 1)
        for p in (left, right):
            if not p.isEmpty():
                p.setColor(color)
        trig = door.find('door_*_trigger')
        if not trig.isEmpty():
            trig.wrtReparentTo(building)

    # -- signs -------------------------------------------------------------
    def _t_sign(self, d, parent):
        front = self._decal_base(parent, ['**/sign_decal', '**/*_front'])
        origin = parent.find('**/*sign_origin')
        if front.isEmpty() or origin.isEmpty():
            self.skipped['sign without sign_origin/front'] += 1
            return None
        code = d.get('code', '')
        if code:
            src = self._find(code)
            if src is None:
                return None
            sign = src.copyTo(front)
            sign.setName('sign')
        else:
            sign = front.attachNewNode(ModelNode('sign'))
        sign.setPosHprScale(origin, d.vec('pos', (0, 0, 0)), _hpr(d), d.vec('scale', (1, 1, 1)))
        sign.setColor(d.vec('color', WHITE))
        self._children(d, sign)
        return sign

    def _t_baseline(self, d, parent):
        graphics = d.find_all('graphic')
        texts = d.find_all('text')
        if texts:
            self.skipped['sign text (baseline with letters)'] += 1
            if graphics:
                self.skipped['sign graphic in text baseline'] += len(graphics)
            return None
        if not graphics:
            return None
        if d.get('width', 0) or d.get('height', 0):
            self.skipped['sign graphic on circular baseline'] += len(graphics)
            return None
        bl = parent.attachNewNode('baseline')
        bl.setColor(d.vec('color', WHITE))
        bscale = d.vec('scale', (1, 1, 1))
        kern = float(d.get('kern', 0) or 0)
        stumble = float(d.get('stumble', 0) or 0)
        stomp = float(d.get('stomp', 0) or 0)
        wiggle = float(d.get('wiggle', 0) or 0)
        cursor = 0.0
        total = 0.0
        for i, g in enumerate(graphics):
            src = self._find(g.get('code', ''))
            odd = i & 1
            gscale = g.vec('scale', (1, 1, 1))
            scale = tuple(a * b for a, b in zip(gscale, bscale))
            pos = list(g.vec('pos', (0, 0, 0)))
            pos[0] += cursor + i * kern + (stumble if odd else -stumble)
            pos[2] += stomp if odd else -stomp
            hpr = list(_hpr(g))
            hpr[2] += wiggle if odd else -wiggle
            w = scale[0] * float(g.get('width', 0) or 0)
            cursor += w
            total += w
            if src is None:
                continue
            gnp = src.copyTo(bl)
            gnp.setPosHprScale(tuple(pos), tuple(hpr), scale)
            gnp.setColor(g.vec('color', d.vec('color', WHITE)))
        # Center the run of graphics on the baseline position.
        n = len(graphics)
        gap = (n - 1) * kern + ((stumble if (n - 1) & 1 else -stumble) if n else 0)
        radius = (total + gap) * 0.5
        pos = list(d.vec('pos', (0, 0, 0)))
        hpr = _hpr(d)
        ang = math.radians(-hpr[2])
        pos[0] -= radius * math.cos(ang)
        pos[2] -= radius * math.sin(ang)
        bl.setPosHprScale(tuple(pos), hpr, (1, 1, 1))
        return bl

    # -- flat buildings ----------------------------------------------------
    def _t_flat_building(self, d, parent):
        self.wall_height = 0.0
        width = float(d.get('width', 10.0))
        bnp = parent.attachNewNode(PandaNode(d.name or 'flat_building'))
        inner = bnp.attachNewNode(PandaNode((d.name or 'flat_building') + '-internal'))
        inner.setScale(width, 1, 1)
        bnp.setPosHpr(d.vec('pos', (0, 0, 0)), _hpr(d))
        for c in d.children:
            if c.kind == 'wall':
                self._wall(c, inner)
            else:
                self.traverse(c, bnp)
        if self.wall_height == 0.0:
            self.skipped['flat_building without walls'] += 1
            return bnp
        barrier = self._find('wall_camera_barrier')
        if barrier is not None:
            b = barrier.copyTo(inner)
            b.setScale(1, 1, self.wall_height)
            b.wrtReparentTo(parent)
        return bnp

    def _wall(self, d, inner):
        src = self._find(d.get('code', ''))
        height = float(d.get('height', 10.0))
        if src is None:
            self.wall_height += height
            return None
        w = src.copyTo(inner)
        w.setPosHprScale((0, 0, self.wall_height), (0, 0, 0), (1, 1, height))
        w.setColor(d.vec('color', WHITE))
        if w.node().isGeomNode():
            w.node().setEffect(DecalEffect.make())
        else:
            g = w.find('**/+GeomNode')
            if not g.isEmpty():
                g.node().setEffect(DecalEffect.make())
        for c in d.children:
            if c.kind == 'windows':
                self._windows(c, w)
            elif c.kind == 'cornice':
                self._cornice(c, w)
            elif c.kind == 'flat_door':
                self._flat_door(c, w)
            else:
                self.traverse(c, w)
        self.wall_height += height
        return w

    def _windows(self, d, wall):
        count = int(d.get('count', 1))
        if count <= 0:
            return
        if count not in _WINDOW_SLOTS:
            self.skipped[f'windows count {count}'] += 1
            return
        code = d.get('code', '')
        width = wall.getParent().getSx()
        base = 1.0 if width <= 5.0 else 1.15 if width <= 10.0 else 1.3
        color = d.vec('color', WHITE)
        # One roll and scale wiggle per windows group, then a position wiggle per window.
        roll = self._jitter(_HPR_JITTER)
        s = base + self._jitter(_SCALE_JITTER)
        for i, (x, z) in enumerate(_WINDOW_SLOTS[count]):
            wcode = code
            if count == 2 and len(code) >= 2:
                # A pair faces each other: left uses the _ur model, right the _ul.
                wcode = code[:-2] + ('ur' if i == 0 else 'ul')
            src = self._find(wcode)
            jx, jz = self._jitter(_POS_JITTER), self._jitter(_POS_JITTER)
            if src is None:
                continue
            win = src.copyTo(wall)
            win.setColor(color)
            win.setScale(NodePath(), s, s, s)
            win.setPos(x + jx, 0, z + jz)
            win.setHpr(0, 0, roll)

    def _cornice(self, d, wall):
        src = self._find(d.get('code', ''))
        if src is None:
            return
        height = wall.getSz()
        width = wall.getParent().getSx()
        top = self.wall_height + height
        color = d.vec('color', WHITE)
        dpart = src.find('**/*_d')
        if not dpart.isEmpty():
            c = dpart.copyTo(wall)
            c.setPosHprScale((0, 0, 1), (0, 0, 0), (1, width / height, width / height))
            c.setColor(color)
        ndpart = src.find('**/*_nd')
        if not ndpart.isEmpty():
            c = ndpart.copyTo(wall.getParent())
            c.setPosHprScale((0, 0, top), (0, 0, 0), (1, width, width))
            c.setColor(color)

    def _flat_door(self, d, wall):
        src = self._find(d.get('code', ''))
        if src is None:
            return
        door = src.copyTo(wall)
        door.setScale(NodePath(), 1, 1, 1)
        door.setPosHpr(0.5, 0, 0, 0, 0, 0)
        door.setColor(d.vec('color', WHITE))

    # DNA 'wall'/'windows'/'cornice' outside a flat building: not meaningful.
    def _t_wall(self, d, parent):
        self.skipped['wall outside flat_building'] += 1

    _t_windows = _t_cornice = _t_flat_door = _t_wall


def _facing(np_, root):
    """Area-weighted mean face normal of visible geometry under np_, in root space."""
    total = Vec3(0, 0, 0)
    for gnp in [np_] + list(np_.findAllMatches('**/+GeomNode')):
        if not isinstance(gnp.node(), GeomNode) or gnp.isHidden():
            continue
        mat = gnp.getMat(root)
        flip = mat.getUpper3().determinant() < 0
        for geom in gnp.node().getGeoms():
            reader = GeomVertexReader(geom.getVertexData(), 'vertex')
            pts = []
            while not reader.isAtEnd():
                pts.append(mat.xformPoint(reader.getData3()))
            for prim in geom.getPrimitives():
                prim = prim.decompose()
                if prim.getNumVerticesPerPrimitive() != 3:
                    continue
                for k in range(prim.getNumPrimitives()):
                    s0 = prim.getPrimitiveStart(k)
                    a, b, c = (pts[prim.getVertex(s0 + j)] for j in range(3))
                    n = (b - a).cross(c - a)
                    total += -n if flip else n
    return total


def lift_decals(root, push=DECAL_PUSH):
    """Move children of every decal-base GeomNode under ``__decalN__`` holders.

    Each holder is also nudged ``push`` units (in root space) along the facing
    direction of its decal geometry, so coplanar decals survive the int16 vertex
    quantization in the baked file instead of z-fighting with their base.
    """
    def visit(np_, depth):
        node = np_.node()
        children = list(np_.getChildren())
        if isinstance(node, GeomNode) and node.getEffect(DecalEffect.getClassType()) and children:
            depth = min(depth + 1, MAX_DECAL_DEPTH)
            holder = np_.attachNewNode(PandaNode(f'__decal{depth}__'))
            for c in children:
                c.reparentTo(holder)
            node.clearEffect(DecalEffect.getClassType())
            if push:
                n = _facing(holder, root)
                if n.length() > 1e-9:
                    n.normalize()
                    inv = Mat4(np_.getMat(root))
                    if inv.invertInPlace():
                        holder.setPos(inv.xformVec(n * push))
            children = [holder]
        for c in children:
            visit(c, depth)
    visit(root, 0)


def cross_billboards(root):
    """Replace BillboardEffect sprites (tree leaves, lamp heads, roof pigs) by static
    two-sided crossed quads, since the baked format has no billboards yet.

    The extra copy is named ``<name>__bbx`` and both are tagged ``billboard=1`` so a
    future runtime billboard path can find them. Returns the number converted.
    """
    count = 0
    for gnp in root.findAllMatches('**/+GeomNode'):
        if not gnp.node().getEffect(BillboardEffect.getClassType()):
            continue
        gnp.node().clearEffect(BillboardEffect.getClassType())
        gnp.setTwoSided(True)
        gnp.setTag('billboard', '1')
        x = gnp.copyTo(gnp.getParent())
        x.setName(gnp.getName() + '__bbx')
        x.setMat(Mat4.rotateMat(90, Vec3(0, 0, 1)) * gnp.getMat())
        count += 1
    return count


def assemble(dna_root, storage, seed=1, name='dna', billboards='cross'):
    """Build a NodePath for a parsed DNA tree. Returns (root, Assembler) for reporting."""
    root = NodePath(PandaNode(name))
    asm = Assembler(storage, seed)
    asm.traverse(dna_root, root)
    lift_decals(root)
    if billboards == 'cross':
        asm.billboards_crossed = cross_billboards(root)
    return root, asm
