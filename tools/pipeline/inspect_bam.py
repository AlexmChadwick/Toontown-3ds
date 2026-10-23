#!/usr/bin/env python3
"""Print geometry/texture/joint stats for Panda3D .bam files.

Used for asset budgeting (see AGENTS.md "Asset pipeline"). Requires the stock
`panda3d==1.10.15` wheel; the upstream Toontown .bam files are BAM 6.24 and
load fine with it. Run with:  python -I tools/pipeline/inspect_bam.py <files...>
"""
import os
import sys

from panda3d.core import CharacterJoint, Filename, loadPrcFileData

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning')
from direct.showbase.ShowBase import ShowBase  # noqa: E402


def stats(base, path):
    np = base.loader.loadModel(Filename.fromOsSpecific(os.path.abspath(path)))
    geoms = verts = tris = 0
    for gnp in np.findAllMatches('**/+GeomNode'):
        gn = gnp.node()
        for i in range(gn.getNumGeoms()):
            g = gn.getGeom(i)
            geoms += 1
            verts += g.getVertexData().getNumRows()
            tris += sum(p.getNumFaces() for p in g.getPrimitives())
    joints = 0
    for cnp in np.findAllMatches('**/+Character'):
        for b in range(cnp.node().getNumBundles()):
            stack = [cnp.node().getBundle(b)]
            while stack:
                part = stack.pop()
                joints += isinstance(part, CharacterJoint)
                stack.extend(part.getChild(i) for i in range(part.getNumChildren()))
    anims = []
    for anp in np.findAllMatches('**/+AnimBundleNode'):
        bundle = anp.node().getBundle()
        anims.append(f'{bundle.getNumFrames()}f@{bundle.getBaseFrameRate():g}fps')
    texs = sorted({(t.getName(), t.getXSize(), t.getYSize(), t.getNumComponents())
                   for t in np.findAllTextures()})
    texbytes = sum(w * h * c for _, w, h, c in texs)
    print(f'{path}\n  geoms={geoms} verts={verts} tris={tris} joints={joints} '
          f'anims={anims} textures={len(texs)} ({texbytes // 1024} KB uncompressed)')
    np.removeNode()


if __name__ == '__main__':
    base = ShowBase()
    for p in sys.argv[1:]:
        stats(base, p)
