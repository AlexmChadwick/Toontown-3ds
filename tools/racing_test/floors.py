"""Original Panda floor-ray oracle, independent of native collision extraction."""
from pathlib import Path
import runpy
import struct
from panda3d.core import BamCache, BitMask32, CollisionNode, CollisionRay, CollisionHandlerQueue, CollisionTraverser, loadPrcFileData
ROOT = Path(__file__).resolve().parents[2]
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').is_dir() else ROOT / 'ref/ref'
loadPrcFileData('', 'window-type none\naudio-library-name null\nmodel-cache-dir\nnotify-level error\nmodel-path ' + str(REF / 'resources'))
from direct.showbase.ShowBase import ShowBase
base = ShowBase()
BamCache.getGlobalPtr().setActive(False)
spec = runpy.run_path(str(REF / 'open-toontown/toontown/racing/RaceGlobals.py'))
output = bytearray()
for track in (0,1,20,21,40,41,60,61,62,63,64,65):
    model = base.loader.loadModel(str(REF / 'resources' / (spec['TrackDict'][track][0] + '.bam')))
    curve = model.find('**/curve_' + ('reverse' if track % 2 else 'forward')).node()
    from panda3d.core import Point3
    ray = CollisionRay(0,0,40000,0,0,-1)
    node = CollisionNode('probe')
    node.setFromCollideMask(BitMask32(2)); node.setIntoCollideMask(BitMask32.allOff()); node.addSolid(ray)
    queue = CollisionHandlerQueue()
    probe = model.attachNewNode(node)
    traverser = CollisionTraverser(); traverser.addCollider(probe,queue)
    for i in range(0,4000,10):
        point = Point3(); curve.getPoint(i / 4000 * (curve.getMaxT() - 1e-11),point)
        ray.setOrigin(point[0], point[1],40000)
        queue.clearEntries(); traverser.traverse(model); queue.sortEntries()
        found = bool(queue.getNumEntries())
        height = queue.getEntry(0).getSurfacePoint(model).z if found else 0
        output += struct.pack('<If', found, height)
    model.removeNode()
(ROOT / 'sdmc-out/racing2/floors.bin').write_bytes(output)
print('Original Panda rays:',len(output)//8)
