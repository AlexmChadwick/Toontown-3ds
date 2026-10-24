#!/usr/bin/env python3
"""Compare decoded full/low Cogs against original Panda actors at mid-walk."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import json
import struct
from battle_common import ROOT,DATA,ttm,write_json
import numpy as np
import preview_ttm
from panda3d.core import GeomVertexWriter,LMatrix4f,NodePath,PNMImage,Texture,TextNode,loadPrcFileData
loadPrcFileData('','window-type offscreen\nload-display p3tinydisplay\nwin-size 240 300')
from direct.showbase.ShowBase import ShowBase
from direct.actor.Actor import Actor
from bake_suit import clothes
from suit_source import source_data,animations


def frames(path):
    data=path.read_bytes();h=struct.unpack_from('<4sHHIHHf3f3fIIIII',data)
    _,_,flags,nv,nf,_,fps,*tail=h
    scale,offset=np.array(tail[:3]),np.array(tail[3:6]);off,stride,attach,_,_=tail[6:]
    positions=np.frombuffer(data,dtype='<i2',offset=off,count=nf*nv*4).reshape(nf,nv,4)[:,:,:3]*scale+offset
    tracks=np.frombuffer(data,dtype='<f4',offset=attach,count=nf*12).reshape(nf,3,4) if flags&1 else None
    return positions,tracks,fps


def apply(model,positions):
    start=0
    for child in model.getChildren():
        vd=child.node().modifyGeom(0).modifyVertexData();w=GeomVertexWriter(vd,'vertex')
        for p in positions[start:start+vd.getNumRows()]:w.setData3(*p)
        start+=vd.getNumRows()
    assert start==len(positions)


def matrix(m):
    return LMatrix4f(*[m[c,r] if c<3 else (1 if r==3 else 0) for r in range(4) for c in range(4)])


def textures(path):
    data=path.read_bytes();n=struct.unpack_from('<H',data,6)[0];off=struct.unpack_from('<I',data,20)[0];out=[]
    for i in range(n):
        w,h,fmt,mips,ws,wt,doff,size,name=struct.unpack_from('<HHBBBBII32s',data,off+i*48)
        px=preview_ttm.untile(data[doff:doff+size],w,h,fmt);image=PNMImage(w,h,4)
        for y in range(h):
            for x in range(w):image.setXelA(x,y,*(px[y,x]/255))
        tex=Texture(name.rstrip(b'\0').decode());tex.load(image);out.append(tex)
    return out


def source_head(base,spec):
    phase=3.5 if spec['body']=='c' else 4
    model=base.loader.loadModel(f"phase_{phase:g}/models/char/suit{spec['body'].upper()}-heads")
    root=NodePath('head')
    for name in spec['parts']:
        for node in model.findAllMatches('**/'+name):
            copy=node.copyTo(root)
            if spec['head_texture']:copy.setTexture(base.loader.loadTexture(f"phase_{phase:g}/maps/{spec['head_texture']}"),1)
            if spec['head_color']:copy.setColor(*spec['head_color'])
    model.removeNode();return root


def main():
    out=ROOT/'sdmc-out/previews/cogs';out.mkdir(parents=True,exist_ok=True)
    base=ShowBase();base.setBackgroundColor(.65,.78,.88,1);base.camLens.setFov(38)
    manifest=json.loads((DATA/'suit/suit_manifest.json').read_text());specs,tables,anim_of=source_data()
    dept_textures=textures(DATA/'suit/departments.ttx')
    cases=('ym','pp','p','tm','f','gh');sheet=PNMImage(720,len(cases)*300,3);metrics=[]
    for row,code in enumerate(cases):
        spec=specs[code];body=spec['body'];dept=spec['dept'];clips,_=animations(body,specs,tables,anim_of)
        source=Actor(f'phase_3.5/models/char/suit{body.upper()}-mod',{'walk':clips['walk']})
        clothes(base,source,dept)
        for node in source.findAllMatches('**/hands'):node.setColor(*spec['hand_color'])
        original_head=source_head(base,spec);original_head.reparentTo(source.find('**/joint_head'))
        source.pose('walk',source.getNumFrames('walk')//2);source.update(force=True);source.setScale(spec['scale'])
        source.reparentTo(base.render);lo,hi=source.getTightBounds();center=(lo+hi)/2
        size=max(hi[2]-lo[2],(hi[0]-lo[0])*1.25)
        base.camera.setPos(center[0]-size*.25,center[1]+size*2.0,center[2]+size*.1);base.camera.lookAt(center)
        images=[]
        for col,lod in enumerate(('source','full','low')):
            if lod=='source':
                root=source
            else:
                source.hide();root=base.render.attachNewNode('baked');root.setScale(spec['scale'])
                entry=manifest['bodies'][body][lod]
                model=preview_ttm.load(str(DATA/'suit'/entry['file']),{4:spec['hand_color']});model.reparentTo(root)
                pos,attach,fps=frames(DATA/'suit'/entry['clips']['walk']['file']);frame=len(pos)//2;apply(model,pos[frame])
                data=(DATA/'suit'/entry['file']).read_bytes();nb=struct.unpack_from('<H',data,10)[0];bo=struct.unpack_from('<I',data,48)[0]
                for b in range(nb):
                    slot=data[bo+b*24+6]
                    if slot:model.getChild(b).setTexture(dept_textures[('c','l','m','s').index(dept)*3+slot-1],1)
                head=preview_ttm.load(str(DATA/'suit'/manifest['heads'][code]['file']),{});head.reparentTo(root)
                head.setMat(matrix(attach[frame]))
            base.graphicsEngine.renderFrame();base.graphicsEngine.renderFrame();image=PNMImage();assert base.win.getScreenshot().store(image)
            images.append(image)
            text=TextNode('label');text.setText(f'{code} / {dept} / {lod}');text.setTextColor(0,0,0,1);text.setAlign(TextNode.ACenter)
            label=base.aspect2d.attachNewNode(text);label.setScale(.07);label.setZ(-.9)
            base.graphicsEngine.renderFrame();base.graphicsEngine.renderFrame();cell=PNMImage();base.win.getScreenshot().store(cell)
            sheet.copySubImage(cell,col*240,row*300);label.removeNode()
            if lod!='source':root.removeNode()
        # Report image-space difference; source uses original textures, baked uses
        # resized ETC1, so exact pixel equality is neither expected nor required.
        def rgb(image):
            return np.array([[tuple(image.getXel(x,y)) for x in range(240)] for y in range(300)])
        arrays=[rgb(i) for i in images]
        metrics.append(dict(code=code,dept=dept,full_mean_abs_rgb=float(np.abs(arrays[0]-arrays[1]).mean()),
                            low_mean_abs_rgb=float(np.abs(arrays[0]-arrays[2]).mean())))
        source.cleanup();source.removeNode()
    assert sheet.write(str(out/'comparison.png'));write_json(out/'comparison.json',dict(version=1,cases=metrics));base.destroy()
    print('wrote comparison.png and comparison.json')

if __name__=='__main__':main()
