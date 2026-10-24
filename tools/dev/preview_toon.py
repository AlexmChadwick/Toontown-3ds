#!/usr/bin/env python3
"""Compose baked toon parts (.ttm + .tta + clothes.ttx) at an animation frame and render a PNG.

Validates the per-part bake (vertex order, quantization, attach transforms, head
fix-ups, tint slots, clothing texture slots) off-device, the way the runtime composes
a toon: legs at the root, torso at legs' attach, head at torso's attach, lashes with
the head, clothing textures rebound from clothes.ttx by each batch's texSlot.

    # one toon from parts (demo colors):
    .venv/bin/python -I tools/dev/preview_toon.py romfs/baked/toon run 3 out.png
    # one toon from a 15-byte DNA netstring (hex), colors/clothes/parts from the DNA:
    .venv/bin/python -I tools/dev/preview_toon.py DIR neutral 0 out.png --dna 74000101...
    # contact sheet: one toon per species (random valid DNAs, fixed seed):
    .venv/bin/python -I tools/dev/preview_toon.py DIR neutral 0 sheet.png --sheet 18
"""
import argparse
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(HERE, '..', 'pipeline'))

import numpy as np  # noqa: E402
from panda3d.core import (GeomVertexWriter, LMatrix4f, PNMImage, Texture,  # noqa: E402
                          TextNode, loadPrcFileData)

import preview_ttm  # noqa: E402  (sets up offscreen Panda config)
loadPrcFileData('', 'win-size 240 320')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

sys.path.insert(0, os.path.join(ROOT, 'ref', 'open-toontown'))
from toontown.toon import ToonDNA  # noqa: E402

CELL_W, CELL_H = 240, 320
BODY_SCALES = {'mouse': 0.6, 'cat': 0.73, 'duck': 0.66, 'rabbit': 0.74, 'horse': 0.85, 'dog': 0.85,
               'monkey': 0.68, 'bear': 0.85, 'pig': 0.77}  # OTPGlobals.toonBodyScales


def load_tta(path):
    buf = open(path, 'rb').read()
    (magic, ver, flags, nv, nf, _, fps, sx, sy, sz, ox, oy, oz,
     frames_off, stride, attach_off, _, _) = struct.unpack_from('<4sHHIHHf3f3fIIIII', buf, 0)
    assert magic == b'TTA1'
    q = np.frombuffer(buf, dtype='<i2', count=nf * nv * 4, offset=frames_off).reshape(nf, nv, 4)
    pos = q[:, :, :3] * np.array([sx, sy, sz]) + np.array([ox, oy, oz])
    attach = None
    if flags & 1:
        attach = np.frombuffer(buf, dtype='<f4', count=nf * 12, offset=attach_off).reshape(nf, 3, 4)
    return pos, attach, fps


def batch_tex_slots(path):
    buf = open(path, 'rb').read()
    nbatch = struct.unpack_from('<H', buf, 10)[0]
    batch_off = struct.unpack_from('<I', buf, 48)[0]
    return [buf[batch_off + 24 * i + 6] for i in range(nbatch)]


def load_ttx(path):
    """clothes.ttx -> [{dna index: panda Texture}] for Shirts, Sleeves, BoyShorts, GirlBottoms."""
    buf = open(path, 'rb').read()
    magic, ver, ntex, c0, c1, c2, c3, map_off, tex_off, _, _ = struct.unpack_from('<4sHH4HII2I', buf, 0)
    assert magic == b'TTX1' and ver == 1
    textures = []
    for i in range(ntex):
        w, h, fmt, mips, ws, wt, doff, dsize, name = struct.unpack_from('<HHBBBBII32s', buf, tex_off + 48 * i)
        px = preview_ttm.untile(buf[doff:doff + dsize], w, h, fmt)
        img = PNMImage(w, h, 4)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[y, x] / 255.0
                img.setXelA(x, y, r, g, b, a)
        tex = Texture(name.rstrip(b'\0').decode())
        tex.load(img)
        tex.setWrapU(Texture.WM_repeat if ws == 2 else Texture.WM_clamp)
        tex.setWrapV(Texture.WM_repeat if wt == 2 else Texture.WM_clamp)
        textures.append(tex)
    flat = struct.unpack_from(f'<{c0 + c1 + c2 + c3}H', buf, map_off)
    tables, k = [], 0
    for c in (c0, c1, c2, c3):
        tables.append({i: textures[t] for i, t in enumerate(flat[k:k + c]) if t != 0xFFFF})
        k += c
    return tables


def apply_positions(model_np, positions):
    """Overwrite vertex positions of a preview_ttm model (batches in file order)."""
    i = 0
    for child in model_np.getChildren():
        gn = child.node()
        vd = gn.modifyGeom(0).modifyVertexData()
        n = vd.getNumRows()
        w = GeomVertexWriter(vd, 'vertex')
        for k in range(n):
            w.setData3(*positions[i + k])
        i += n
    assert i == len(positions), (i, len(positions))


def to_panda(m34):
    # Column-vector 3x4 -> Panda row-vector 4x4.
    return LMatrix4f(m34[0][0], m34[1][0], m34[2][0], 0, m34[0][1], m34[1][1], m34[2][1], 0,
                     m34[0][2], m34[1][2], m34[2][2], 0, m34[0][3], m34[1][3], m34[2][3], 1)


class Spec:
    """What to draw: part codes, tints {slot: rgba}, clothing textures {texSlot: (table, index)}."""

    def __init__(self, legs='m', torso='ms', head='dls', lashes=None, tints=None, clothes=None,
                 scale=1.0, label=''):
        self.legs, self.torso, self.head, self.lashes = legs, torso, head, lashes
        self.tints = tints if tints is not None else preview_ttm.DEMO_TINTS
        self.clothes, self.scale, self.label = clothes or {}, scale, label


def spec_from_dna(netstring):
    """Mirrors source/world/toon_dna.cpp partCodes()/tints using the upstream tables."""
    d = ToonDNA.ToonDNA()
    d.makeFromNetString(netstring)
    torso = d.torso
    if len(torso) == 1:
        torso += 's'  # naked torsos are not shipped
    bottom_table = 2
    if d.gender == 'f':
        bottom_table = 3
        skirt = ToonDNA.GirlBottoms[d.botTex][1] == ToonDNA.SKIRT
        torso = torso[0] + ('d' if skirt else 's')
    col = lambda c: tuple(c)  # noqa: E731
    tints = {1: col(d.getHeadColor()), 2: col(d.getArmColor()), 3: col(d.getGloveColor()),
             4: col(d.getLegColor()), 5: col(ToonDNA.ClothesColors[d.topTexColor]),
             6: col(ToonDNA.ClothesColors[d.sleeveTexColor]), 7: col(ToonDNA.ClothesColors[d.botTexColor])}
    clothes = {1: (0, d.topTex), 2: (1, d.sleeveTex), 3: (bottom_table, d.botTex)}
    return Spec(d.legs, torso, d.head, d.head[:2] if d.gender == 'f' else None, tints, clothes,
                BODY_SCALES[d.getAnimal()], f'{d.head} {torso} {d.legs} {d.gender}')


def build(base, spec, data_dir, anim, frame, lod, ttx):
    root = base.render.attachNewNode('toon')
    root.setScale(spec.scale)
    parent = root
    for kind, code in (('legs', spec.legs), ('torso', spec.torso), ('head', spec.head)):
        name = f'{kind}_{code}_{lod}'
        path = os.path.join(data_dir, name + '.ttm')
        model = preview_ttm.load(path, spec.tints)
        if kind == 'torso' and ttx is not None:
            for b, slot in enumerate(batch_tex_slots(path)):
                if slot and slot in spec.clothes:
                    table, index = spec.clothes[slot]
                    model.getChild(b).setTexture(ttx[table].get(index, ttx[table][0]), 1)
        clip = os.path.join(data_dir, f'{name}_{anim}.tta')
        attach = None
        if os.path.exists(clip):  # non-dog heads are static
            pos, attach, _ = load_tta(clip)
            f = frame % len(pos)
            apply_positions(model, pos[f])
        model.reparentTo(parent)
        if kind == 'head' and spec.lashes:
            preview_ttm.load(os.path.join(data_dir, f'lashes_{spec.lashes}.ttm'), {}).reparentTo(parent)
        if attach is not None:
            child_root = parent.attachNewNode('attach')
            child_root.setMat(to_panda(attach[f]))
            parent = child_root
    return root


def snapshot(base):
    base.graphicsEngine.renderFrame()
    base.graphicsEngine.renderFrame()
    img = PNMImage()
    base.win.getScreenshot().store(img)
    return img


def random_dnas(n, seed):
    rng = random.Random(seed)
    species = list(ToonDNA.toonSpeciesTypes)
    out = []
    for i in range(n):
        sp = species[i % len(species)]
        d = ToonDNA.ToonDNA()
        gender = 'm' if i % 2 == 0 else 'f'
        heads = ToonDNA.getHeadList(sp)
        head = heads[(i // len(species)) % len(heads)] if n > len(species) else rng.choice(heads)
        torso = rng.choice('sml') + ('s' if gender == 'm' else rng.choice('sd'))
        d.newToon((head, torso, rng.choice('sml'), gender), rng.choice(ToonDNA.defaultBoyColorList))
        d.armColor = rng.choice(ToonDNA.defaultBoyColorList)
        d.legColor = rng.choice(ToonDNA.defaultBoyColorList)
        d.topTex, d.sleeveTex = rng.randrange(len(ToonDNA.Shirts)), rng.randrange(len(ToonDNA.Sleeves))
        d.topTexColor, d.sleeveTexColor = (rng.randrange(len(ToonDNA.ClothesColors)) for _ in range(2))
        d.botTex = rng.randrange(len(ToonDNA.BoyShorts) if gender == 'm' else len(ToonDNA.GirlBottoms))
        d.botTexColor = rng.randrange(len(ToonDNA.ClothesColors))
        out.append(d.makeNetString())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('anim')
    ap.add_argument('frame', type=int)
    ap.add_argument('png')
    ap.add_argument('--legs', default='m')
    ap.add_argument('--torso', default='ms')
    ap.add_argument('--head', default='dls')
    ap.add_argument('--lashes', help="lashes code, e.g. 'cl' (species + head length)")
    ap.add_argument('--lod', default='500')
    ap.add_argument('--dna', help='15-byte netstring as hex')
    ap.add_argument('--sheet', type=int, help='render N random valid DNAs into a grid')
    ap.add_argument('--seed', type=int, default=3)
    ap.add_argument('--cols', type=int, default=6)
    ap.add_argument('--all-heads', action='store_true', help='one toon per toonHeadType (female: lashes)')
    ap.add_argument('--closeup', action='store_true', help='frame the head')
    args = ap.parse_args()
    base = ShowBase()
    base.setBackgroundColor(0.41, 0.69, 0.85, 1)
    base.camLens.setFov(36)
    ttx_path = os.path.join(args.dir, 'clothes.ttx')
    ttx = load_ttx(ttx_path) if os.path.exists(ttx_path) else None

    if args.all_heads:
        specs = []
        for i, head in enumerate(ToonDNA.toonHeadTypes):
            d = ToonDNA.ToonDNA()
            d.newToon((head, 'ms' if i % 2 == 0 else 'md', 'm', 'm' if i % 2 == 0 else 'f'), 1 + i % 24)
            specs.append(spec_from_dna(d.makeNetString()))
    elif args.sheet:
        specs = [spec_from_dna(s) for s in random_dnas(args.sheet, args.seed)]
    elif args.dna:
        specs = [spec_from_dna(bytes.fromhex(args.dna))]
    else:
        specs = [Spec(args.legs, args.torso, args.head, args.lashes, label=f'{args.head} {args.torso} {args.legs}')]

    cells = []
    for spec in specs:
        toon = build(base, spec, args.dir, args.anim, args.frame, args.lod, ttx)
        if args.closeup:
            top = toon.getTightBounds()[1][2]
            base.camera.setPos(-1.0, 3.2, top - 0.35)
            base.camera.lookAt(0, 0, top - 0.55)
        else:
            base.camera.setPos(-2.8, 8.2, 2.6)
            base.camera.lookAt(0, 0, 1.55)
        label = TextNode('label')
        label.setText(spec.label)
        label.setTextColor(0, 0, 0, 1)
        label.setAlign(TextNode.ACenter)
        lnp = base.aspect2d.attachNewNode(label)
        lnp.setScale(0.08)
        lnp.setPos(0, 0, -0.93)
        cells.append(snapshot(base))
        lnp.removeNode()
        toon.removeNode()

    cols = min(args.cols, len(cells))
    rows = (len(cells) + cols - 1) // cols
    w, h = cells[0].getXSize(), cells[0].getYSize()
    sheet = PNMImage(w * cols, h * rows)
    sheet.fill(1, 1, 1)
    for i, img in enumerate(cells):
        sheet.copySubImage(img, (i % cols) * w, (i // cols) * h)
    sheet.write(args.png)
    for s in specs:
        print(s.label)
    print('wrote', args.png)


if __name__ == '__main__':
    main()
