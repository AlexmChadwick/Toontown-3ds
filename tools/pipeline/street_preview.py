#!/usr/bin/env python3
"""Render decoded TZ chunk TTM bytes with Panda offscreen, never source geometry."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
import sys
sys.dont_write_bytecode = True
import tempfile
import argparse
import struct
import json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import numpy as np
import tz


def load_model(data, cache):
    """Vectorized TTM decode; cache repeated atlas pages between chunk models."""
    import preview_ttm
    from panda3d.core import (Texture,NodePath,Geom,GeomNode,GeomVertexData,GeomVertexFormat,
                              GeomTriangles,AlphaTestAttrib,RenderAttrib,TransparencyAttrib)
    h=struct.unpack_from('<4sHHHHII6f5I',data)
    absolute=bool(h[2] & 1)
    nt,nb,nv,ni=h[3:7]
    sx,sy,sz,ox,oy,oz=h[7:13]
    tt,bt,vb,ib=h[13:17]
    textures=[]
    for i in range(nt):
        w,height,fmt,mips,ws,wt,off,n,name=struct.unpack_from('<HHBBBBII32s',data,tt+i*48)
        raw=data[off:off+n]
        key=(w,height,fmt,ws,wt,raw)
        if key not in cache:
            pixels=preview_ttm.untile(raw,w,height,fmt)
            tex=Texture(name.split(b'\0')[0].decode())
            tex.setup2dTexture(w,height,Texture.T_unsigned_byte,Texture.F_rgba8)
            tex.setRamImageAs(np.flipud(pixels).tobytes(),'RGBA')
            tex.setWrapU(Texture.WM_repeat if ws==2 else Texture.WM_clamp)
            tex.setWrapV(Texture.WM_repeat if wt==2 else Texture.WM_clamp)
            cache[key]=tex
        textures.append(cache[key])
    source=np.frombuffer(data,dtype=[('p','<i2',4),('uv','<i2',2),('c','u1',4)],count=nv,offset=vb)
    vertices=np.zeros(nv,dtype=[('p','<f4',3),('c','u1',4),('uv','<f4',2)])
    vertices['p']=source['p'][:,:3]*np.array([sx,sy,sz])+np.array([ox,oy,oz])
    vertices['c']=source['c']
    vertices['uv']=source['uv']/1024
    vd=GeomVertexData('zone',GeomVertexFormat.getV3c4t2(),Geom.UHStatic)
    assert vd.getFormat().getArray(0).getStride()==vertices.dtype.itemsize
    vd.modifyArray(0).modifyHandle().setData(vertices.tobytes())
    indices=np.frombuffer(data,dtype='<u2',count=ni,offset=ib)
    root=NodePath('ttm')
    for i in range(nb):
        tex,blend,flags,tint,layer,slot,pad,fv,nverts,fi,nidx=struct.unpack_from('<H6B4I',data,bt+i*24)
        primitive=GeomTriangles(Geom.UHStatic)
        primitive.setIndexType(Geom.NTUint32)
        raw=indices[fi:fi+nidx].astype('<u4')
        if not absolute:
            raw=raw+fv
        primitive.modifyVertices().modifyHandle().setData(raw.tobytes())
        geom=Geom(vd)
        geom.addPrimitive(primitive)
        node=GeomNode('batch')
        node.addGeom(geom)
        part=root.attachNewNode(node)
        if tex!=65535:
            part.setTexture(textures[tex])
        if blend==1:
            part.setAttrib(AlphaTestAttrib.make(RenderAttrib.M_greater,.5))
        elif blend==2:
            part.setTransparency(TransparencyAttrib.M_alpha)
        if flags&1:
            part.setTwoSided(True)
        if layer:
            if blend==2:
                part.setBin('fixed',layer)
            else:
                part.setBin('opaque',layer)
            part.setDepthOffset(layer)
    return root


def ground_view(zone, cell):
    """Choose an open floor triangle and face along the clearest camera ray."""
    floors=[f for f in zone['floors'] if f[9]==cell]
    if zone['branch']%1000==0:
        level=float(np.median([np.mean(np.array(f[:9]).reshape(3,3)[:,2]) for f in zone['floors']]))
        floors=[f for f in floors if np.mean(np.array(f[:9]).reshape(3,3)[:,2])>=level-.1] or floors
    def area(f):
        tri=np.array(f[:9]).reshape(3,3)
        return abs(float(np.cross(tri[1]-tri[0],tri[2]-tri[0])[2]))
    floor=max(floors,key=area)
    p=np.array(floor[:9]).reshape(3,3).mean(axis=0)
    eye=p+np.array([0,0,7])
    collision=zone['collision']
    h=struct.unpack_from('<4sHH3fHH4I6f8I',collision)
    records=np.frombuffer(collision,dtype=[('v','<f4',(3,3)),('n','<f4',3),('d','<f4'),('flags','<u4')],count=h[8],offset=h[18])
    triangles=records['v'][(records['flags']&4)!=0].astype(float)
    best=-1
    direction=np.array([0,1,0])
    for angle in np.arange(16)*np.pi/8:
        d=np.array([np.cos(angle),np.sin(angle),0.0])
        a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
        e1,e2=b-a,c-a
        cross=np.cross(d,e2)
        det=np.einsum('ij,ij->i',e1,cross)
        valid=np.abs(det)>1e-8
        inv=np.zeros_like(det); inv[valid]=1/det[valid]
        diff=eye-a
        u=np.einsum('ij,ij->i',diff,cross)*inv
        q=np.cross(diff,e1)
        v=(q@d)*inv
        t=np.einsum('ij,ij->i',e2,q)*inv
        hit=valid&(u>=0)&(v>=0)&(u+v<=1)&(t>0)
        score=min(60,float(t[hit].min())) if hit.any() else 60
        if score>best:
            best=score; direction=d
    return tuple(eye),tuple(eye+direction*35+np.array([0,0,-1])),floor


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('zone')
    parser.add_argument('out')
    parser.add_argument('--lod',choices=('high','low','planned'),default='planned')
    parser.add_argument('--eye', type=float, nargs=3, help='explicit zone-space camera')
    parser.add_argument('--look', type=float, nargs=3, help='explicit zone-space target')
    parser.add_argument('--signs',action='store_true')
    parser.add_argument('--cell',type=int,help='server cell for ground camera')
    args=parser.parse_args()
    path,out=Path(args.zone),Path(args.out)
    import street_lod
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
    models={}
    cache={}
    for i,c in enumerate(zone['chunks']):
        for lod in (0,1) if zone['version']>=2 else (0,):
            offset=c['low_offset'] if lod else c['offset']
            size=c['low_size'] if lod else c['size']
            if not size:
                continue
            model=load_model(tz.model_bytes(data,offset,size),cache)
            model.reparentTo(group)
            models[i,lod]=model
    lo,hi=group.getTightBounds()
    centre=(lo+hi)*.5
    extent=max(hi.x-lo.x,hi.y-lo.y)
    views=[('top',(centre.x,centre.y-1,extent*1.2),(centre.x,centre.y,0),None)]
    # Use large open floor triangles; an offset behind the toon can put the
    # camera inside the building bordering a street cell.
    cells=sorted({f[9] for f in zone['floors'] if args.cell is None or f[10]==args.cell})
    if zone['branch']%1000==0:
        level=float(np.median([np.mean(np.array(f[:9]).reshape(3,3)[:,2]) for f in zone['floors']]))
        cells=[cell for cell in cells if any(f[9]==cell and np.mean(np.array(f[:9]).reshape(3,3)[:,2])>=level-.1 for f in zone['floors'])] or cells
        def centrality(cell):
            floors=[f for f in zone['floors'] if f[9]==cell and np.mean(np.array(f[:9]).reshape(3,3)[:,2])>=level-.1]
            f=max(floors or [f for f in zone['floors'] if f[9]==cell],key=lambda f:np.linalg.norm(np.cross(np.array(f[3:6])-f[:3],np.array(f[6:9])-f[:3])))
            p=np.array(f[:9]).reshape(3,3).mean(axis=0)
            return float(np.sum((p[:2]-np.array([centre.x,centre.y]))**2))
        cells.sort(key=centrality)
    for cell in cells[:3]:
        pos,look,floor=ground_view(zone,cell)
        views.append((f'cell_{floor[10]}_{cell}',pos,look,floor))
    if args.signs:
        metadata=json.loads(path.with_suffix('.json').read_text())
        for i,v in enumerate(metadata.get('sign_views',[])[:3]):
            floor=min(zone['floors'],key=lambda f:float(np.sum((np.array(f[:9]).reshape(3,3).mean(axis=0)[:2]-np.array(v['eye'])[:2])**2)))
            views.append((f'sign_{i}',v['eye'],v['look'],floor))
    if args.eye is not None:
        if args.look is None:
            parser.error('--eye requires --look')
        floor = min(zone['floors'], key=lambda f: float(np.sum((np.array(f[:9]).reshape(3,3).mean(axis=0)-args.eye)**2)))
        views = [('explicit',args.eye,args.look,floor)]
    out.mkdir(parents=True,exist_ok=True)
    for name,pos,look,floor in views:
        choices=None
        if args.lod=='planned' and floor is not None and zone['version']>=2:
            feet=np.array(floor[:9]).reshape(3,3).mean(axis=0)
            choices,_=street_lod.plan(zone['chunks'],floor[9],feet)
        for (i,lod),model in models.items():
            want=(choices.get(i,-1) if choices is not None else (1 if args.lod=='low' and zone['version']>=2 else 0))
            model.show() if lod==want else model.hide()
        base.camera.setPos(*map(float,pos))
        base.camera.lookAt(*map(float,look))
        base.camLens.setFov(65)
        base.camLens.setNearFar(.5,3000)
        for _ in range(3):
            base.graphicsEngine.renderFrame()
        target=out/f'{zone["branch"]}_{args.lod}_{name}.png'
        if not base.win.saveScreenshot(str(target)):
            raise RuntimeError(f'cannot save {target}')
        print(target,flush=True)
    base.destroy()


if __name__=='__main__':
    main()
