"""Extract playground movers in pivot-local coordinates before static zone baking.

Metadata is an offline handoff; runtime formats TTM1, TTC1 and TZ2 are unchanged.
"""
import json
from pathlib import Path

TROLLEY_HOODS = (1000, 2000, 3000, 4000, 5000, 9000)


def split(root, zone, output):
    import assemble
    import collide
    import extract
    import ttm
    from panda3d.core import NodePath

    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    movers = []

    def bake(node, stem, anchor, frame):
        if node.isEmpty():
            raise ValueError(f'{zone}: missing moving prop {stem}')
        position = list(map(float, frame.getPos(root)))
        heading = float(frame.getH(root))
        holder = NodePath('moving_prop')
        copy = node.copyTo(holder)
        # Preserve descendant transforms but remove the pivot's authored pose.
        copy.clearMat()
        ex = extract.Extractor(max_tex=128, layer_rules=assemble.LAYER_RULES)
        ex.add(holder)
        stats = ttm.write(str(output / (stem + '.ttm')), ex.textures, ex.batches())
        if stats['uv_clamped']:
            raise ValueError(f'{stem}: UV overflow')
        mesh = collide.extract(holder, fallback_floor=False)
        collision = None
        if mesh.tris or mesh.caps or mesh.planes:
            collision = mesh.write(str(output / (stem + '.ttc')), cell_size=4, max_bytes=256*1024)
        record = dict(anchor=anchor, model=stem+'.ttm', collision=stem+'.ttc' if collision else None,
                      position=position, heading=heading, hpr=list(map(float, frame.getHpr(root))),
                      scale=list(map(float, frame.getScale(root))), geometry=stats,
                      collision_stats=collision, local_position=list(map(float,node.getPos())),
                      parent_position=list(map(float,node.getParent().getPos(root))),
                      parent_hpr=list(map(float,node.getParent().getHpr(root))),
                      parent_scale=list(map(float,node.getParent().getScale(root))),
                      local_hpr=list(map(float,node.getHpr())), local_scale=list(map(float,node.getScale())))
        movers.append(record)
        holder.removeNode()
        node.stash()

    if zone in TROLLEY_HOODS:
        cars = list(root.findAllMatches('**/trolley_car'))
        if len(cars) != 1:
            raise ValueError(f'{zone}: expected one trolley car, found {len(cars)}')
        car = cars[0]
        # The direct parent is the station model, including its DNA transform.
        bake(car, f'{zone}_trolley', 'trolley_station', car.getParent())
    if zone == 1000:
        for side in ('east', 'west'):
            node = root.find('**/'+side+'_pier')
            bake(node, f'1000_pier_{side}', 'pier_'+side, node)
            movers[-1]['animation_hpr_local'] = [90 if side=='east' else -90, .25, 0]
            movers[-1]['raised_pitch'] = -44.2601
            movers[-1]['flat_pitch'] = .25
            movers[-1]['travel_seconds'] = 5
    if movers:
        (output/f'{zone}_moving.json').write_text(json.dumps(movers, indent=2)+'\n')
    return movers


def append_points(path, movers):
    with open(path, 'a') as stream:
        for m in movers:
            x,y,z = m['position']
            stream.write(f"{m['anchor']} {x:.6f} {y:.6f} {z:.6f} {m['heading']:.6f}\n")
    return len(movers)
