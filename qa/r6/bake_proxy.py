"""Render the baked near (or far) chunks of a .tz at a gate near view with Panda (stock_detail lens)."""
import math, os, struct, sys, tempfile
from pathlib import Path
ROOT = Path('/workspace/tt-agents/claude-3ds-gates/Toontown-3ds')
sys.path.insert(0, str(ROOT/'tools/pipeline')); sys.path.insert(0, str(ROOT/'tools/fidelity'))
import numpy as np
from PIL import Image
import preview_ttm, tz, near_views
from panda3d.core import SamplerState
from direct.showbase.ShowBase import ShowBase
tzpath, view, out = sys.argv[1], sys.argv[2], sys.argv[3]
lod = os.environ.get('LOD', 'near')
data = Path(tzpath).read_bytes()
h = tz.HEADER.unpack_from(data)
nchunks, co = h[4], h[8]
v = near_views.views()[view]
base = ShowBase(); base.setBackgroundColor(0.55, 0.75, 1, 1)
fovy = 55; half = math.radians(fovy/2)
base.camLens.setFov(2*math.degrees(math.atan(math.tan(half)*400/240)), fovy); base.camLens.setNearFar(1, 1500)
eye = np.array(v['eye']); n = 0; ids = {}
for i in range(nchunks):
    c = tz.CHUNK.unpack_from(data, co + i*tz.CHUNK_V2_SIZE)
    lo = tz.LOW.unpack_from(data, co + i*tz.CHUNK_V2_SIZE + 80)
    off, size = (c[3], c[4]) if lod == 'near' else (lo[0], lo[1])
    if not size: continue
    bmin, bmax = np.array(c[9:12]), np.array(c[12:15])
    if np.linalg.norm(np.clip(eye, bmin, bmax) - eye) > float(os.environ.get('RADIUS', 200)): continue
    f = tempfile.NamedTemporaryFile(suffix='.ttm', delete=False); f.write(tz.model_bytes(data, off, size)); f.close()
    m = preview_ttm.load(f.name, {}); os.unlink(f.name)
    if os.environ.get('SRCTEX'):
        from panda3d.core import Filename
        for npth in m.findAllMatches('**/+GeomNode'):
            t=npth.getTexture()
            if t and not t.getName().startswith(('local_atlas','phase_')):
                import glob
                f=glob.glob(os.environ['SRCTEX']+'/phase_*/maps/'+t.getName()+'.jpg')
                if f:
                    nt=base.loader.loadTexture(Filename.fromOsSpecific(f[0])); nt.setWrapU(t.getWrapU()); nt.setWrapV(t.getWrapV()); npth.setTexture(nt,1)
    for t in m.findAllTextures():
        if not os.environ.get('PREVIEW_MIPS'): t.setMinfilter(SamplerState.FT_linear_mipmap_linear)
        t.setMagfilter(SamplerState.FT_linear)
        if os.environ.get('NOMIP'): t.setMinfilter(SamplerState.FT_linear)
        if os.environ.get('ANISO'): t.setAnisotropicDegree(int(os.environ['ANISO']))
        if os.environ.get('GBIAS') and t.getName() in os.environ.get('GNAMES','').split(','):
            sm=SamplerState(t.getDefaultSampler()); sm.setLodBias(float(os.environ['GBIAS'])); t.setDefaultSampler(sm)
        if os.environ.get('LODBIAS'):
            sm=SamplerState(t.getDefaultSampler()); sm.setLodBias(float(os.environ['LODBIAS'])); t.setDefaultSampler(sm)
    if os.environ.get('IDS'):
        from panda3d.core import TextureStage
        for k,npth in enumerate(m.findAllMatches('**/+GeomNode')):
            t=npth.getTexture(); key=(t.getName() if t else 'none')+'@%d'%i
            ids.setdefault(key,len(ids)+1); q=ids[key]
            npth.setTextureOff(1); npth.setColorOff(1); npth.setColor(((q*37)%256)/255.,((q*91)%256)/255.,((q*53)%256)/255.,1,10)
    m.reparentTo(base.render); n += 1
base.camera.setPos(*v['eye']); base.camera.lookAt(*v['target'])
base.graphicsEngine.renderFrame(); base.graphicsEngine.renderFrame()
base.win.saveScreenshot(out)
g = np.asarray(Image.open(out).convert('RGB')).astype(float).mean(2)
import json; json.dump(ids, open(out+'.ids.json','w'))
print(view, lod, 'chunks', n, 'detail', round(near_views.detail(g), 3))
