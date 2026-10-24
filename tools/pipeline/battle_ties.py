"""Department tie textures for optional skeleton actors, using TTM texture slot 4."""
import sys
sys.dont_write_bytecode=True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import struct
import json
from battle_common import DATA,ttm,write_json
import extract


def finalize(base,out,manifest):
    textures=[]
    for dept,suffix in [('c','boss'),('l','legal'),('m','money'),('s','sales')]:
        tex=base.loader.loadTexture(f'phase_5/maps/cog_robot_tie_{suffix}.jpg')
        px,n,fmt=extract.texture_pixels(tex,64)
        textures.append(ttm.Texture(f'{dept}_tie',px,extract.choose_format(px,n,fmt)))
    texoff=40;cursor=ttm._align(texoff+4*48);data=bytearray(cursor)
    struct.pack_into('<4sHH4HII2I',data,0,b'TTX1',1,4,1,1,1,1,32,texoff,0,0)
    struct.pack_into('<4H',data,32,0,1,2,3)
    for i,t in enumerate(textures):
        struct.pack_into('<HHBBBBII32s',data,texoff+i*48,t.w,t.h,t.fmt,t.mips,t.wrap_s,t.wrap_t,cursor,len(t.data),t.name.encode().ljust(32,b'\0'))
        data.extend(t.data);cursor=ttm._align(len(data));data.extend(bytes(cursor-len(data)))
    (out/'skeleton_ties.ttx').write_bytes(data)
    for variant,lods in manifest['bodies'].items():
        if 'skeleton' not in variant:continue
        for entry in lods.values():
            path=out/entry['file'];data=bytearray(path.read_bytes())
            nt,nb=struct.unpack_from('<HH',data,8);toff,boff=struct.unpack_from('<II',data,44)
            tie_ids={i for i in range(nt) if 'tie' in struct.unpack_from('<HHBBBBII32s',data,toff+i*48)[-1].rstrip(b'\0').decode().lower()}
            assert tie_ids,(path,'missing tie texture')
            for i in range(nb):
                if struct.unpack_from('<H',data,boff+i*24)[0] in tie_ids:data[boff+i*24+6]=4
            path.write_bytes(data);entry['tie_texture_slot']=4
    manifest['skeleton_ties']=dict(file='skeleton_ties.ttx',bytes=(out/'skeleton_ties.ttx').stat().st_size,
        departments=['c','l','m','s'],slot=4)
    return manifest

if __name__=='__main__':
    import argparse
    from direct.showbase.ShowBase import ShowBase
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,default=DATA/'suit');args=ap.parse_args()
    base=ShowBase();manifest=json.loads((args.out/'suit_manifest.json').read_text())
    write_json(args.out/'suit_manifest.json',finalize(base,args.out,manifest));base.destroy()
