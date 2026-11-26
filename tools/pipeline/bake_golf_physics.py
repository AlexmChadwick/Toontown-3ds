#!/usr/bin/env python3
"""Bake the stock MiniGolf ODE collision nodes to versioned GPH1 files.

GPH1 is little-endian: <4sHHI header (magic, version, reserved, triangles), then
triangle records <9fB3x.  Surface values mirror GolfGlobals: grass=0, hard=2,
cup=3, slick=4.  The runtime has no Panda or ODE dependency.

One hole_<id>.gph per GolfGlobals.HoleInfo entry: holes that share a terrain keep different
blockers, and GolfHoleBase builds the hard-surface mesh after loadBlockers removed the others
(bake_golf.apply_blockers, the same rule as the drawn hole_<id>.ttm).
"""
import argparse
import struct
import sys
from pathlib import Path

from panda3d.core import GeomNode, GeomVertexReader, Filename, loadPrcFileData
from direct.showbase.ShowBase import ShowBase

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bake_golf  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = next((p for p in (ROOT / "ref" / "resources", ROOT / "ref" / "ref" / "resources")
                  if (p / "phase_6").exists()), ROOT / "ref" / "resources")
OUT = ROOT / "sdmc-out" / "3ds" / "toontown3ds" / "data" / "golf"
SURFACES = (("softSurface/**/grass*", 0), ("softSurface/**/slick*", 4),
            ("softSurface/**/hole*", 3), ("hardSurface/**/+GeomNode", 2))

def triangles(root, pattern, surface):
    found = root.findAllMatches("**/" + pattern)
    for path in found:
        node = path.node()
        if not isinstance(node, GeomNode):
            continue
        matrix = path.getMat(root)
        for gi in range(node.getNumGeoms()):
            geom = node.getGeom(gi).decompose()
            verts = GeomVertexReader(geom.getVertexData(), "vertex")
            points = []
            while not verts.isAtEnd():
                points.append(matrix.xformPoint(verts.getData3f()))
            prim = geom.getPrimitive(0).decompose()
            for pi in range(0, prim.getNumPrimitives()):
                start, end = prim.getPrimitiveStart(pi), prim.getPrimitiveEnd(pi)
                for i in range(start, end - 2, 3):
                    a, b, c = (points[prim.getVertex(i + j)] for j in range(3))
                    yield (a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, surface)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--resources", default=str(RESOURCES))
    ap.add_argument("--out", default=str(OUT))
    args = ap.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    loadPrcFileData("", "window-type none\naudio-library-name null")
    base = ShowBase()
    report = []
    hole_info = bake_golf.load_hole_info(args.resources)
    for hole in sorted(hole_info):
        path = Path(args.resources) / hole_info[hole]["terrainModel"]
        model = base.loader.loadModel(Filename.fromOsSpecific(str(path)), noCache=True)
        bake_golf.apply_blockers(model, hole_info[hole])
        data = []
        for pattern, surface in SURFACES:
            data.extend(triangles(model, pattern, surface))
        target = out / ("hole_%d.gph" % hole)
        with target.open("wb") as f:
            f.write(struct.pack("<4sHHI", b"GPH1", 1, 0, len(data)))
            for tri in data:
                f.write(struct.pack("<9fB3x", *tri))
        report.append((hole, len(data)))
        model.removeNode()
    print("golf physics:", " ".join("%d=%d" % pair for pair in report))

if __name__ == "__main__":
    main()
