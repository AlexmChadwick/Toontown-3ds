#!/usr/bin/env python3
"""Bake battle props and a single ETC1A4 inventory atlas plus UV mapping."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import argparse
from battle_common import DATA,ROOT,capture,ttm,literal_table,write_json,bake_actor
import numpy as np
from panda3d.core import NodePath,OrthographicLens,PNMImage,TransparencyAttrib,loadPrcFileData
loadPrcFileData('', 'window-type offscreen\nload-display p3tinydisplay\nwin-size 28 28\nframebuffer-alpha true')
from direct.showbase.ShowBase import ShowBase
WANTED = (
    'feather', 'megaphone', 'lipstick', 'lips', 'hat', 'cane', 'cubes', 'ladder',
    'banana', 'rake', 'marbles', 'quicksand', 'trapdoor', 'tnt', 'traintrack',
    '1dollar', 'big-magnet', 'hypno-goggles', 'slideshow',
    'bikehorn', 'whistle', 'bugle', 'aoogah', 'elephant', 'fog_horn', 'singing',
    'tart', 'fruitpie-slice', 'creampie-slice',
    'squirting-flower', 'glass', 'water-gun', 'bottle',
    'flowerpot', 'sandbag', 'anvil', 'weight', 'safe', 'piano', 'ship',
    'splat', 'button', 'propeller', 'kapow', 'splash', 'phone', 'receiver', 'explosion'
)


def atlas(base,out,preview):
    model=base.loader.loadModel('phase_3.5/models/gui/inventory_icons')
    icons=sorted(model.findAllMatches('**/+GeomNode'),key=lambda n:n.getName())
    sheet=PNMImage(256,256,4);sheet.fill(0,0,0);sheet.alphaFill(0)
    base.setBackgroundColor(0,0,0,0)
    lens=OrthographicLens();base.cam.node().setLens(lens);lens.setNearFar(.01,100)
    mapping={}
    assert len(icons)<=64
    for i,node in enumerate(icons):
        holder=NodePath('icon');node.copyTo(holder);holder.reparentTo(base.render)
        holder.setTransparency(TransparencyAttrib.M_alpha)
        lo,hi=holder.getTightBounds();center=(lo+hi)/2
        size=max(hi[0]-lo[0],hi[2]-lo[2])
        assert size>0
        lens.setFilmSize(size*1.02,size*1.02)
        base.camera.setPos(center[0],center[1]-10,center[2]);base.camera.lookAt(center)
        base.graphicsEngine.renderFrame();base.graphicsEngine.renderFrame()
        image=PNMImage();assert base.win.getScreenshot().store(image) and image.hasAlpha()
        x,y=(i%8)*32+2,(i//8)*32+2
        sheet.copySubImage(image,x,y)
        mapping[node.getName()]=dict(rect=[x,y,28,28],uv=[(x+.5)/256,1-(y+27.5)/256,(x+27.5)/256,1-(y+.5)/256])
        holder.removeNode()
    sheet.write(str(preview/'inventory_atlas.png'))
    px=np.empty((256,256,4),dtype=np.uint8)
    for y in range(256):
        for x in range(256):
            c=sheet.getXelA(x,y);px[y,x]=[round(v*255) for v in c]
    texture=ttm.Texture('inventory_atlas',px,ttm.GPU_ETC1A4,ttm.GPU_CLAMP_TO_EDGE,ttm.GPU_CLAMP_TO_EDGE,mipmaps=False)
    verts=np.array([[-1,0,-1,0,0,1,1,1,1],[1,0,-1,1,0,1,1,1,1],
                    [1,0,1,1,1,1,1,1,1],[-1,0,1,0,1,1,1,1,1]],dtype=float)
    batch=ttm.Batch(0,ttm.BLEND_ALPHA,True,0,0,verts,np.array([0,1,2,0,2,3]))
    stats=ttm.write(out/'inventory_atlas.ttm',[texture],[batch])
    metadata=dict(magic='GAG_ATLAS',version=1,width=256,height=256,format='ETC1A4',mips=1,
        image='inventory_atlas.ttm',origin='image top left; UV v=1 at top',icons=mapping,**stats)
    tracks=literal_table('toontown/toonbase/ToontownBattleGlobals.py','AvPropsNew')
    metadata['gags']=[dict(track=t,level=l,icon=name) for t,row in enumerate(tracks) for l,name in enumerate(row)]
    assert all(entry['icon'] in mapping for entry in metadata['gags'])
    write_json(out/'inventory_atlas.json',metadata)
    model.removeNode()
    return metadata


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=Path,default=DATA/'props')
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    preview=ROOT/'sdmc-out/previews/cogs';preview.mkdir(parents=True,exist_ok=True)
    base=ShowBase();rows=literal_table('toontown/battle/BattleProps.py','Props')
    entries={r[1]:r for r in rows};entries['tart']=(3.5,'tart','tart');entries['kapow']=(5,'kapow','kapow-mod','kapow-chan');entries['splash']=(5,'splash','splash')
    # MovieUtil.createKapowExplosionTrack: the Cog-death burst (static card, billboarded at runtime).
    entries['explosion']=(3.5,'explosion','explosion')
    entries['ship']=(5,'ship','ship')
    manifest=dict(version=1,props={})
    for name in WANTED:
        row=entries[name];model=f'phase_{row[0]:g}/models/props/{row[2]}'
        if len(row)==4:
            clip=f'phase_{row[0]:g}/models/props/{row[3]}'
            info=bake_actor(base,args.out,name,model,{'action':clip},{'full':4000})['full']
        else:
            prop=base.loader.loadModel(model);root=NodePath('prop_root');prop.reparentTo(root)
            if name=='tart':prop.setScale(.5)
            if name=='lips':prop.setZ(-3.04)
            ex,batches=capture(root,True,animated=False)
            filename=name+'.ttm';info=dict(file=filename,source_model=model,clips={},**ttm.write(args.out/filename,ex.textures,batches))
            root.removeNode()
        manifest['props'][name]=info
    manifest['atlas']=atlas(base,args.out,preview)
    manifest['splat_variants']={
        'tart':dict(scale=.3,color=[55/255,40/255,148/255,1]),
        'fruitpie-slice':dict(scale=.5,color=[55/255,40/255,148/255,1]),
        'creampie-slice':dict(scale=.5,color=[250/255,241/255,24/255,1]),
        'splash-from-splat':dict(scale=1,color=[.75,.75,1,1])}
    write_json(args.out/'props_manifest.json',manifest);base.destroy()

if __name__=='__main__':main()
