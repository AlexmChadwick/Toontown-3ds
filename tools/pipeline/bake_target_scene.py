#!/usr/bin/env python3
"""Bake the Toon Slingshot (Target) minigame scene as DistributedTargetGame builds it.

    .venv/bin/python -I tools/pipeline/bake_target_scene.py [--resources ref/resources] \
        [--out sdmc-out/3ds/toontown3ds/data/minigames] [--header source/game/target_scene.h]

load() prepares slingshot_game_desert4 (cloud_layer1, backdrop_layer1 and both mountains hidden,
the sky at z 76, cloud_layer_alpha at z 63 turned over, a copy of it at z 50, the block moved to
y -5 and scaled 15 x 15 x 2), onstage() lays 6 rows of 7 copies of that block with a mountain in
the second column out on each side (sand hidden in the outer columns), and setupTargets()
draws the score rings (a disc and rings, vertex colour times the pattern's colour at alpha 0.5),
trampolines over black jump circles and 30 cacti from the AI's seed.

Writes target_sky.ttm, target_sand.ttm, target_mount_left.ttm, target_mount_right.ttm (in the
mountain node's frame), target_cactus_a.ttm, target_cactus_b.ttm, target_ring_<hood>_<t>.ttm
(target type t; t 4 is the jump circle) and the header with every block and
mountain matrix, the hood patterns and the jump sizes. onstage's mountain heights come from the
unseeded client random; here random.seed(0) picks them.
"""
import argparse
import ast
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

from panda3d.core import (Filename, NodePath, loadPrcFileData, GeomVertexFormat, GeomVertexData,  # noqa: E402
                          GeomVertexWriter, GeomTriangles, Geom, GeomNode, TransparencyAttrib)

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                'textures-power-2 none')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import extract  # noqa: E402
import ttm  # noqa: E402

MG = 'phase_4/models/minigames/'
ENVIRON_LENGTH, ENVIRON_WIDTH = 300.0, 150.0
HOODS = {'ToontownCentral': 2000, 'DonaldsDock': 1000, 'DaisyGardens': 5000,
         'MinniesMelodyland': 4000, 'TheBrrrgh': 3000, 'DonaldsDreamland': 9000}


def patterns(upstream):
    """TargetGameGlobals.difficultyPatterns with the colour dicts, by hood id."""
    tree = ast.parse(open(os.path.join(upstream, 'toontown/minigame/TargetGameGlobals.py')).read())
    colors, pats = {}, None
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Subscript):
            name = node.targets[0].value.id
            colors.setdefault(name, {})[node.targets[0].slice.value] = node.value.value
        elif isinstance(node, ast.Assign) and getattr(node.targets[0], 'id', '') == 'difficultyPatterns':
            pats = node.value
    out = {}
    for k, v in zip(pats.keys, pats.values):
        p = []
        for e in v.elts:
            if isinstance(e, ast.List):
                p.append([colors[x.id] if isinstance(x, ast.Name) else x.value for x in e.elts])
            else:
                p.append(e.value)
        out[HOODS[k.attr]] = p
    return out


def circle_points(seg_count, radius):
    pts = [(radius * math.cos(2 * math.pi * s / seg_count), radius * math.sin(2 * math.pi * s / seg_count))
           for s in range(seg_count)]
    return pts + [pts[0]]


def add_disc(vw, cw, tris, base, radius, inner, rgba, vertex_count):
    """addCircle (inner 0) / addRing (inner = radius - thickness) as triangles at z 0.025."""
    pts = circle_points(5 + int(vertex_count), radius)
    n = 0
    if inner <= 0:
        vw.addData3f(0, 0, 0.025)
        cw.addData4f(*rgba)
        for x, y in pts:
            vw.addData3f(x, y, 0.025)
            cw.addData4f(*rgba)
        for i in range(len(pts) - 1):
            tris.addVertices(base, base + 1 + i, base + 2 + i)
        return 1 + len(pts)
    ipts = circle_points(5 + int(vertex_count), inner)
    for (x, y), (ix, iy) in zip(pts, ipts):
        vw.addData3f(x, y, 0.025)
        cw.addData4f(*rgba)
        vw.addData3f(ix, iy, 0.025)
        cw.addData4f(*rgba)
        n += 2
    for i in range(len(pts) - 1):
        a, b, c, d = base + 2 * i, base + 2 * i + 1, base + 2 * i + 2, base + 2 * i + 3
        tris.addVertices(a, c, b)
        tris.addVertices(b, c, d)
    return n


def ring_model(pat):
    """One GeomNode per target type (setupTargets' circle then rings), then the jump circle."""
    counts, values, sizes, cols, subparts, jump_size = pat[0], pat[1], pat[2], pat[3], pat[4], pat[5]
    root = NodePath('rings')
    shapes = []
    for t in range(len(counts)):
        parts = []
        sub, first = subparts[t], True
        while sub:
            ci = t + sub - 1
            c = cols[ci]
            rgba = (c['Red'], c['Green'], c['Blue'], c['Alpha'])
            inner = 0 if first else sizes[ci] - abs(sizes[ci] - sizes[ci + 1])
            parts.append((sizes[ci], inner, rgba, sizes[ci] / 2 + 20))
            first = False
            sub -= 1
        shapes.append(parts)
    shapes.append([(jump_size + 0.5, 0, (0, 0, 0, 0.25), jump_size / 2 + 20)])
    for i, parts in enumerate(shapes):
        vd = GeomVertexData('ring', GeomVertexFormat.getV3c4(), Geom.UHStatic)
        vw, cw = GeomVertexWriter(vd, 'vertex'), GeomVertexWriter(vd, 'color')
        tris = GeomTriangles(Geom.UHStatic)
        base = 0
        for radius, inner, rgba, vcount in parts:
            base += add_disc(vw, cw, tris, base, radius, inner, rgba, vcount)
        g = Geom(vd)
        g.addPrimitive(tris)
        gn = GeomNode('type%d' % i)
        gn.addGeom(g)
        np_ = root.attachNewNode(gn)
        np_.setTransparency(TransparencyAttrib.MAlpha)
        np_.setDepthWrite(False)
    return root


def write(name, node, out_dir, max_tex=256, each_node=False):
    ex = extract.Extractor(max_tex=max_tex)
    ex.add(node)
    stats = ttm.write(os.path.join(out_dir, name + '.ttm'), ex.textures, ex.batches())
    print(name, stats)
    return stats


def mat12(np_, rel):
    m = np_.getMat(rel)  # row vectors (p' = p * M)
    return [m.getCell(r, c) for c in range(3) for r in range(4)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'sdmc-out/3ds/toontown3ds/data/minigames'))
    ap.add_argument('--header', default=os.path.join(ROOT, 'source/game/target_scene.h'))
    args = ap.parse_args()
    res = os.path.realpath(args.resources)
    upstream = os.path.join(os.path.dirname(res), 'open-toontown')
    base = ShowBase()
    load = lambda p: base.loader.loadModel(Filename.fromOsSpecific(os.path.join(res, p + '.bam')))

    # ---- DistributedTargetGame.load ----
    env = load(MG + 'slingshot_game_desert4')
    env.find('**/cloud_layer1').hide()
    env.find('**/backdrop_layer1').hide()
    env.find('**/mountain_set_b').hide()
    env.find('**/mountain_c').hide()
    env.find('**/sand_layer1').setZ(-0.05)
    sky = env.find('**/blue_sky_layer1')
    sky.setZ(76.0)
    cloud = env.find('**/cloud_layer_alpha')
    low = env.attachNewNode('skyLow')
    cloud.copyTo(low)
    cloud.setZ(63.0)
    cloud.setTwoSided(True)
    cloud.setR(180)
    low.setZ(50.0)
    low.setTwoSided(True)
    env.setPos(0, -5.0, 0)
    env.flattenMedium()
    env.setScale(15.0, 15.0, 2.0)

    # ---- the drawn parts, each in its own frame ----
    # In the block's own frame: the kBlocks matrices carry its 15 x 15 x 2 scale.
    holder = NodePath('h')
    part = env.copyTo(holder)
    part.clearTransform()
    for n in ('**/sand_layer1', '**/mountain_set_b', '**/mountain_c', '**/cloud_layer1', '**/backdrop_layer1'):
        part.find(n).removeNode()
    write('target_sky', holder, args.out)
    holder.removeNode()
    holder = NodePath('h')
    part = env.copyTo(holder)
    part.find('**/sand_layer1').reparentTo(holder)
    part.removeNode()
    write('target_sand', holder, args.out)
    holder.removeNode()
    mounts = {}
    for side, path, sand in (('left', '**/mountain_set_b', '**/sand_b'), ('right', '**/mountain_c', '**/sand_c')):
        m = env.find(path)
        holder = NodePath('h')
        c = m.copyTo(holder)
        c.clearTransform()
        c.show()
        c.find(sand).removeNode()
        write('target_mount_' + side, holder, args.out)
        holder.removeNode()
        mounts[side] = m

    # ---- DistributedTargetGame.onstage: the block grid and mountains ----
    random.seed(0)
    node = NodePath('environNode')
    blocks, mount_mats = [], []
    max_i = 4
    for i in range(-1, max_i + 1):
        inst = env.copyTo(node)
        inst.setY(ENVIRON_LENGTH * i)
        blocks.append((mat12(inst, node), 1))
        for sign, side in ((-1, 'left'), (1, 'right')):
            j_range = 3
            for j in range(j_range):
                inst = env.copyTo(node)
                inst.setY(ENVIRON_LENGTH * i)
                inst.setX(sign * ENVIRON_WIDTH * (j + 1))
                height = random.random() * 5.0 + 5.0
                random.random()  # heading (unused upstream)
                blocks.append((mat12(inst, node), 0 if j == j_range - 1 else 1))
                if j != j_range - 2:
                    continue
                mount = inst.find('**/mountain_set_b' if side == 'left' else '**/mountain_c')
                d = 8.5 * -sign  # left: getX() + 8.5, right: getX() - 8.5
                mount.setScale(1.0, 1.1, height)
                mount.setPos(mount.getX() + d, 0, 0.0)
                if i >= max_i - 1:
                    mount.setHpr(310 if side == 'left' else 50, 0, 0)
                    mount.setScale(1.0, 1.5, height * 1.2)
                    mount.setPos(mount.getX() + 2.0 * sign, 0, 0.0)
                    if i == max_i:
                        continue  # mount.hide()
                if i == -1:
                    mount.setHpr(50 if side == 'left' else 310, 0, 0)
                    mount.setScale(1.0, 1.5, height * 1.2)
                    mount.setPos(mount.getX() - 5.0 * sign, 3.0, 0.0)
                mount_mats.append((mat12(mount, node), 0 if side == 'left' else 1))

    # ---- setupTargets: score rings, jump circle and the cacti ----
    pats = patterns(upstream)
    for hood, pat in sorted(pats.items()):
        rings = ring_model(pat)
        for t, child in enumerate(rings.getChildren()):
            holder = NodePath('h')
            child.copyTo(holder)
            write('target_ring_%d_%d' % (hood, t), holder, args.out)
            holder.removeNode()
    cacti = load(MG + 'slingshot_game_cacti')
    for name in ('cactus_a', 'cactus_b'):
        holder = NodePath('h')
        c = cacti.find('**/' + name).copyTo(holder)
        c.clearTransform()
        write('target_' + name, holder, args.out)
        holder.removeNode()

    with open(args.header, 'w') as f:
        f.write('// Generated by tools/pipeline/bake_target_scene.py: DistributedTargetGame.onstage block grid\n'
                '// (row vectors, m[r * 4 + c] with column 3 the translation) and TargetGameGlobals patterns.\n'
                '#pragma once\n\nnamespace tt::targetgame::scene {\n\n')
        f.write('struct Placed {\n    float m[12];\n    int kind; // blocks: 1 with sand; mountains: 0 left, 1 right\n};\n\n')
        for name, rows in (('kBlocks', blocks), ('kMountains', mount_mats)):
            f.write('constexpr Placed %s[] = {\n' % name)
            for m, kind in rows:
                f.write('    {{%s}, %d},\n' % (', '.join('%.4ff' % v for v in m), kind))
            f.write('};\n')
        f.write('\nstruct HoodPattern {\n    unsigned hood;\n    int counts[4];\n    float sizes[4];\n'
                '    int subParts[4];\n    float jumpSize;\n    int jumpNum;\n};\n\n'
                'constexpr HoodPattern kPatterns[] = {\n')
        for hood, p in sorted(pats.items()):
            f.write('    {%d, {%s}, {%s}, {%s}, %.2ff, %d},\n' % (
                hood, ', '.join(str(v) for v in p[0]), ', '.join('%.2ff' % v for v in p[2]),
                ', '.join(str(v) for v in p[4]), p[5], p[6]))
        f.write('};\n\n} // namespace tt::targetgame::scene\n')
    print('blocks', len(blocks), 'mountains', len(mount_mats))


if __name__ == '__main__':
    main()
