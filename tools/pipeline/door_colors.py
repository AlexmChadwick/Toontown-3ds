"""The colour of each door's swinging leaves, as .pts lines ("color:<door point> r g b a").

DNADoor.setupDoor tints a door's leaves (door_*_left / door_*_right) with the DNA door colour,
or the interior's TI_door colour. The runtime draws the leaves itself while a door opens
(world/transition.cpp), so it needs that colour per door. The "color:" prefix keeps these lines
out of every name-prefix lookup on the anchors (door_origin, tb / sz blocks, linktunnel_).
"""
from panda3d.core import ColorAttrib

MAX_DIST = 6.0  # a door's leaf hinges sit ~3.6 units either side of its origin (x0.8 scaled)


def _leaves(root):
    out = []
    for n in root.findAllMatches('**/door_*_right;+h') + root.findAllMatches('**/door_*_left;+h'):
        if n.getName().endswith(('_hole_right', '_hole_left')):
            continue
        attrib = n.getState().getAttrib(ColorAttrib)
        if attrib is None or attrib.getColorType() != ColorAttrib.T_flat:
            continue
        out.append((n.getPos(root), attrib.getColor()))
    return out


def lines(root, doors):
    """doors: [(point name, LPoint3 position in root)]. One line per door with a coloured leaf
    near it; doors whose own model carries its leaves (Toon HQ exteriors) get none."""
    leaves = _leaves(root)
    result = []
    for name, p in doors:
        best = None
        for q, c in leaves:
            d = (q - p).length()
            if d <= MAX_DIST and (best is None or d < best[0]):
                best = (d, c)
        if best:
            c = best[1]
            result.append(f'color:{name} {c[0]:.3f} {c[1]:.3f} {c[2]:.3f} {c[3]:.3f}')
    return result
