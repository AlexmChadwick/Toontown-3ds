#!/usr/bin/env python3
"""Bake assets used by place transitions (tunnels, doors, teleports, death).

Outputs:
  romfs/baked/portal.ttm         Portable hole prop for teleport in/out
  romfs/baked/door_right.ttm     Swinging right door leaf for building/interior doors
  romfs/baked/audio/enc_lose.tsnd Sad/death sound effect (ENC_Lose.ogg)

Usage:
  .venv/bin/python -I tools/pipeline/bake_transitions.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

from panda3d.core import (  # noqa: E402
    Filename, GeomVertexRewriter, LMatrix4f, NodePath, loadPrcFileData
)

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\ntextures-power-2 none')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import bake_audio  # noqa: E402
import extract  # noqa: E402
import ttm  # noqa: E402


def find_resources():
    if os.environ.get('TT_RES'):
        return os.environ['TT_RES']
    d = ROOT
    while True:
        cand = os.path.join(d, 'ref', 'resources')
        if (os.path.isdir(cand) and os.listdir(cand)) or os.path.dirname(d) == d:
            return cand
        cand = os.path.join(d, 'ref', 'ref', 'resources')
        if (os.path.isdir(cand) and os.listdir(cand)):
            return cand
        d = os.path.dirname(d)


def bake_portal(base, res_dir, out_path):
    """Bake the portable hole (phase_3.5/models/props/portal-mod.bam) centered at (0, 0, 0)."""
    mod_path = os.path.join(res_dir, 'phase_3.5', 'models', 'props', 'portal-mod.bam')
    chan_path = os.path.join(res_dir, 'phase_3.5', 'models', 'props', 'portal-chan.bam')

    m = base.loader.loadModel(Filename.fromOsSpecific(mod_path))
    c = base.loader.loadModel(Filename.fromOsSpecific(chan_path))

    # Channel table for scale_portal joint
    table = c.find('**/+AnimBundleNode').node().getBundle().findChild('scale_portal')
    mat = LMatrix4f()
    table.getValue(50, mat)  # Frame 50 is on-the-floor fully open

    gn = m.find('**/+GeomNode').node()
    geom = gn.modifyGeom(0)
    vdata = geom.modifyVertexData()
    rw = GeomVertexRewriter(vdata, 'vertex')
    while not rw.isAtEnd():
        v = rw.getData3f()
        v_world = mat.xformPoint(v)
        # Center in X and Y around (0, 0) and lie flat on Z = 0
        rw.setData3f(v_world[0] - 0.072, v_world[1] - 2.445, 0.0)

    root = NodePath('portal_centered')
    root.attachNewNode(gn)
    ex = extract.Extractor(max_tex=128)
    ex.add(root)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    stats = ttm.write(out_path, ex.textures, ex.batches())
    print(f"Baked portal: {stats['file_bytes']} bytes, {stats['triangles']} tris -> {out_path}")


def bake_door_leaf(base, res_dir, out_path):
    """Bake right door leaf (phase_3.5/models/modules/doors_practical.bam) with hinge at origin."""
    bam_path = os.path.join(res_dir, 'phase_3.5', 'models', 'modules', 'doors_practical.bam')
    m = base.loader.loadModel(Filename.fromOsSpecific(bam_path))
    d = m.find('**/door_double_round_ur_right')
    d.setPos(0, 0, 0)
    d.setHpr(0, 0, 0)

    ex = extract.Extractor(max_tex=128)
    ex.add(d)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    stats = ttm.write(out_path, ex.textures, ex.batches())
    print(f"Baked door leaf: {stats['file_bytes']} bytes, {stats['triangles']} tris -> {out_path}")


def bake_death_sfx(res_dir, out_path):
    """Bake death/sadness sound effect (phase_5/audio/sfx/ENC_Lose.ogg) as ADPCM .tsnd."""
    src = os.path.join(res_dir, 'phase_5', 'audio', 'sfx', 'ENC_Lose.ogg')
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    bake_audio.bake_sfx_adpcm(src, out_path, False)
    print(f"Baked death sfx -> {out_path}")


def main():
    res = find_resources()
    print(f"Using resources at: {res}")
    base = ShowBase()

    portal_out = os.path.join(ROOT, 'romfs', 'baked', 'portal.ttm')
    door_out = os.path.join(ROOT, 'romfs', 'baked', 'door_right.ttm')
    lose_out = os.path.join(ROOT, 'romfs', 'baked', 'audio', 'enc_lose.tsnd')

    bake_portal(base, res, portal_out)
    bake_door_leaf(base, res, door_out)
    bake_death_sfx(res, lose_out)
    print("All transition assets baked successfully.")


if __name__ == '__main__':
    main()
