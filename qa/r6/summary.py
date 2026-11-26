import sys,numpy as np
from PIL import Image
from scipy import ndimage
sys.path.insert(0,'/workspace/tt-agents/claude-3ds-gates/Toontown-3ds/tools/fidelity')
import near_views as nv
def g(p): return np.asarray(Image.open(p).convert('RGB'),float).mean(2)
names=sys.argv[1].split(','); ims=[g(p) for p in sys.argv[2:]]
edges=nv.edge_map(ims[0])|nv.edge_map(ims[1])   # stock | 3DS, as the gate (old|new identical)
far=nv._keep(ims[0].shape)&~ndimage.binary_dilation(edges,iterations=3)
keep=nv._keep(ims[0].shape)
reg={'awning cell x280-320 y0-40':(slice(0,40),slice(280,320)),'ground y160-240':(slice(160,240),slice(0,400))}
for n,i in zip(names,ims):
    L=nv._laplace(i)
    for tag,mask in (('(a)',keep),('(b)',far)):
        m=L[mask].mean(); N=mask.sum(); E=((L-m)**2)*mask; tot=E.sum()/N
        parts={k:E[s].sum()/N for k,s in reg.items()}
        print(f"{n:6s} {tag} total {tot:10.6f} | "+' | '.join(f'{k} {v:9.6f}' for k,v in parts.items())+f' | rest {tot-sum(parts.values()):10.6f}')
