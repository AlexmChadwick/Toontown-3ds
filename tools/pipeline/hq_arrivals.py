"""Read upstream HoodMgr arrival policy without importing/running client Python."""
import ast
from pathlib import Path

HOODS = {10000:'BossbotHQ',11000:'SellbotHQ',12000:'CashbotHQ',13000:'LawbotHQ'}


def upstream_point(resources, zone):
    tree = ast.parse((Path(resources).parent/'open-toontown/toontown/distributed/HoodMgr.py').read_text())
    cls = next(n for n in tree.body if isinstance(n,ast.ClassDef) and n.name=='HoodMgr')
    values = {}
    for n in cls.body:
        if isinstance(n,ast.Assign):
            for target in n.targets:
                if isinstance(target,ast.Name):
                    values[target.id] = n.value
    points = values['dropPoints']
    for key,value in zip(points.keys,points.values):
        if isinstance(key,ast.Attribute) and key.attr==HOODS[zone]:
            return ast.literal_eval(value)[0], 'HoodMgr.dropPoints.'+HOODS[zone]+'[0]'
    return ast.literal_eval(values['DefaultDropPoint']), 'HoodMgr.DefaultDropPoint (Bossbot has no entry)'


def check_floor(mesh, point):
    """Reject arrivals outside a standable floor at the authored arrival level."""
    import numpy as np
    import collide
    x,y,z = point[:3]
    heights = []
    for a,b,c,n,flags in mesh.tris:
        if not flags & collide.FLOOR or flags & collide.TRIGGER or n[2]<.707:
            continue
        triangle = np.array([a,b,c])
        edge = (triangle[1:,:2]-triangle[0,:2]).T
        if abs(np.linalg.det(edge))<1e-9:
            continue
        u,v = np.linalg.solve(edge,np.array([x,y])-triangle[0,:2])
        if min(u,v)>=-1e-5 and u+v<=1.00001:
            height = triangle[0,2]+u*(triangle[1,2]-triangle[0,2])+v*(triangle[2,2]-triangle[0,2])
            if height<=z+4:
                heights.append(float(height))
    if not heights or abs(z-max(heights))>.15:
        raise ValueError(f'unsafe HQ arrival {point}: floor heights {heights}')
    return max(heights)


def write_anchor(resources, zone, path, mesh):
    if zone not in HOODS:
        return None
    point,source = upstream_point(resources,zone)
    floor = check_floor(mesh,point)
    x,y,z,h,p,r = point
    if p or r:
        raise ValueError('arrival requires pitch/roll')
    # Normalize equivalent headings for the runtime table.
    h = (h+180)%360-180
    with open(path,'a') as stream:
        stream.write(f'hq_drop {x:.6f} {y:.6f} {z:.6f} {h:.6f}\n')
    return dict(position=[x,y,z],heading=h,source=source, floor=floor)
