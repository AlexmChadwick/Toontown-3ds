#!/usr/bin/env python3
"""Render decoded TZ chunk TTM bytes with Panda offscreen, never source geometry."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import sys
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import numpy as np
import tz


def main():
    path,out=Path(sys.argv[1]),Path(sys.argv[2])
    data=path.read_bytes()
    zone=tz.validate(data)
    import preview_ttm
    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'win-size 960 720\nmodel-cache-dir\nload-display p3tinydisplay')
    from direct.showbase.ShowBase import ShowBase
    base=ShowBase()
    if base.win is None:
        raise RuntimeError('Panda offscreen buffer unavailable')
    base.setBackgroundColor(.41,.69,.85,1)
    group=base.render.attachNewNode('zone')
    with tempfile.TemporaryDirectory() as tmp:
        for i,c in enumerate(zone['chunks']):
            if not c['size']:
                continue
            p=Path(tmp)/f'{i}.ttm'
            p.write_bytes(data[c['offset']:c['offset']+c['size']])
            model=preview_ttm.load(str(p),{})
            for b in model.getChildren():
                if b.getBinName()=='fixed' and not b.hasTransparency():
                    b.setBin('opaque',b.getBinDrawOrder())
            model.reparentTo(group)
    lo,hi=group.getTightBounds()
    centre=(lo+hi)*.5
    extent=max(hi.x-lo.x,hi.y-lo.y)
    views=[('top',(centre.x,centre.y-1,extent*1.2),(centre.x,centre.y,0))]
    # Ground cameras at real floor samples in three distinct street regions.
    used=[]
    for f in zone['floors']:
        if f[9] in used:
            continue
        if zone['branch']==2100 and f[10] not in (2101,2114,2124):
            continue
        used.append(f[9])
        p=np.array(f[:9]).reshape(3,3).mean(axis=0)
        views.append((f'cell_{f[10]}_{f[9]}',(p[0]-15,p[1]-25,p[2]+12),(p[0]+20,p[1]+15,p[2]+6)))
        if len(views)>=4:
            break
    out.mkdir(parents=True,exist_ok=True)
    for name,pos,look in views:
        base.camera.setPos(*map(float,pos))
        base.camera.lookAt(*map(float,look))
        base.camLens.setFov(65)
        base.camLens.setNearFar(.5,3000)
        for _ in range(3):
            base.graphicsEngine.renderFrame()
        target=out/f'{zone["branch"]}_{name}.png'
        if not base.win.saveScreenshot(str(target)):
            raise RuntimeError(f'cannot save {target}')
        print(target,flush=True)
    base.destroy()


if __name__=='__main__':
    main()
