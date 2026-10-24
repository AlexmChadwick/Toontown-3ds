"""Versioned JSON text (.paths) export of the upstream directed walk graphs."""
import json
from char_source import NAMES, path_namespace

# name, localized path selector, location, zone, speed; April Fools swaps follow.
ROUTES = [
 ('mk','Mickey',0,2000,5), ('vmk','VampireMickey',0,2000,1.15),
 ('mn','Minnie',0,4000,3.2), ('wmn','WitchMinnie',0,4000,1.8),
 ('g','Goofy',0,5000,5.2), ('g','Goofy',1,8000,5.2), ('sg','SuperGoofy',1,8000,1.6),
 ('d','Donald',0,9000,3.68), ('fd','FrankenDonald',0,9000,.9),
 ('dw','DonaldDock',0,1000,0), ('p','Pluto',1,3000,5.5), ('wp','WesternPluto',1,3000,3.2),
 ('dd','Daisy',0,5000,2.3), ('shdd','SockHopDaisy',0,5000,2.3),
 ('ch','Chip',0,6000,3), ('pch','PoliceChip',0,6000,3),
 ('da','Dale',0,6000,3.5), ('jda','JailbirdDale',0,6000,3.5),
 ('mk','Daisy',0,5000,5), ('mn','Pluto',0,3000,3.2),
 ('dd','Mickey',0,2000,2.3), ('d','Goofy',1,8000,3.68),
 ('g','Donald',0,9000,5.2), ('p','Minnie',0,4000,5.5)]


def export(out):
    ns = path_namespace()
    reports = []
    for code, selector, location, zone, speed in ROUTES:
        graph = ns['getPaths'](selector, location)
        nodes = {key: {'position': list(value[0]), 'adjacent': list(value[1])}
                 for key, value in sorted(graph.items())}
        edges = []
        for src, node in nodes.items():
            for dst in node['adjacent']:
                assert dst in nodes
                points = ns['getPointsFromTo'](src, dst, graph)
                lengths = [(b-a).length() for a,b in zip(points, points[1:])]
                duration = sum(lengths)/speed if speed else 0
                if speed:
                    assert abs(duration-ns['getWalkDuration'](src,dst,speed,graph)) < 1e-6
                edges.append(dict(src=src,dst=dst,points=[list(p) for p in points],
                                  raycast=ns['getRaycastFlag'](src,dst,graph),
                                  segment_distances=lengths,distance=sum(lengths),nominal_seconds=duration))
        data = dict(magic='CCHAR_PATHS',version=1,character=NAMES[code],dna_code=code,
                    zone=zone,selector=selector,location=location,speed=speed,start='a',nodes=nodes,edges=edges)
        if code in ('da','jda'):
            data['follow_chip'] = dict(radius=3,initial_offset=[3,0],distance_per_revolution=13,
                radius_overrides=[dict(src=a,dst=b,radius=r) for (a,b),r in ns['DaleOrbitDistanceOverride'].items()])
        if code == 'dw':
            data['parent'] = 'DonaldsBoat'
            data['parent_position'] = [0,-1,3.95]
        path = out / f'{NAMES[code]}_{zone}.paths'
        path.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n')
        reports.append(dict(file=path.name,nodes=len(nodes),edges=len(edges),speed=speed))
    return reports
