"""Animated DNA props (TownLoader / SafeZoneLoader.createAnimatedProps) for the zone bakers.

Two cases, as the stock client runs them on this server (HolidayManagerAI runs no holidays):

* interactive_prop_* (hydrants, mailboxes, trash cans on streets): InteractiveAnimatedProp.enter
  stops the actor and poses it at its hood's 'idle0' frame 0 unless HYDRANTS / MAILBOXES /
  TRASHCANS_BUFF_BATTLES runs. They stay in the static mesh; freeze_interactive() poses them.
  ('idle0' is ZoneToIdles[hood][0]; a hood without idles keeps the DNA anim's frame 0.)
  A playground's interactive props are GenericAnimatedProps (SafeZoneLoader) and loop; none of
  this DNA has any.
* animated_prop_generic_* (GenericAnimatedProp: loops the DNA anim; under Python 3 its
  "type(self).__name__ == 'instance'" holiday test never matches) and the Toon HQ periscope /
  telescope (HQPeriscopeAnimatedProp / HQTelescopeAnimatedProp: a frame-range Sequence) move,
  and so does each playground pet shop's window fish (PetShopFishAnimatedProp loops 'swim').
  split() bakes each kind once to data/animprops/<stem>.ttm + <stem>.tta (12 fps vertex
  animation, tta.py; long clips at 8 / 6 / 4 fps
  to stay under CLIP_BYTES) and the zone's instances to <zone>.tap, then stashes the nodes so the
  static chunks leave them out. world/anim_props.* plays them.

<zone>.tap (text, one instance per line): "<stem> <kind> m00 m01 m02 m03 ... m23" with the
3x4 row-major prop-to-zone matrix (row vectors as Panda: p' = p * M, written transposed so
column 3 is the translation). kind 0 loops the clip, 1 / 2 run the periscope / telescope track.
"""
import ast
import math
from pathlib import Path

FPS = 12.0
# Linear memory per clip: long clips step down to 8 / 6 / 4 fps (the runtime interpolates).
CLIP_BYTES = 800 * 1024
FPS_STEPS = (12.0, 8.0, 6.0, 4.0)
KIND_LOOP, KIND_PERISCOPE, KIND_TELESCOPE = 0, 1, 2

_CLASSES = {'hydrant': 'HydrantInteractiveProp', 'trashcan': 'TrashcanInteractiveProp',
            'mailbox': 'MailboxInteractiveProp'}
_tables = {}


def _upstream(resources):
    return Path(resources).resolve().parent / 'open-toontown' / 'toontown'


def _hood_ids(resources):
    ids = {}
    tree = ast.parse((_upstream(resources) / 'toonbase/ToontownGlobals.py').read_text())
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            if isinstance(node.value, ast.Constant) and isinstance(node.value.value, int):
                ids[node.targets[0].id] = node.value.value
    return ids


def idle_tables(resources):
    """{class name: {hood id: first idle anim name}} from each class's ZoneToIdles."""
    key = str(resources)
    if key in _tables:
        return _tables[key]
    ids = _hood_ids(resources)
    out = {}
    for cls in _CLASSES.values():
        tree = ast.parse((_upstream(resources) / f'hood/{cls}.py').read_text())
        table = {}
        for node in ast.walk(tree):
            if (isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
                    and node.targets[0].id == 'ZoneToIdles'):
                for k, v in zip(node.value.keys, node.value.values):
                    hood = ids[k.attr]
                    first = v.elts[0]
                    name = first.elts[0] if isinstance(first, ast.Tuple) else first
                    table[hood] = name.value
        if not table:
            raise ValueError(f'{cls}: no ZoneToIdles')
        out[cls] = table
    _tables[key] = out
    return out


def _model_dir(code):
    """GenericAnimatedProp's self.path for a DNA code (phase_5/models/char)."""
    for prefix in ('interactive_prop_', 'animated_prop_generic_'):
        if code.startswith(prefix):
            path = code[len(prefix):].split('__')[0]
            break
    else:
        raise ValueError(code)
    cut = len('phase_') + path[len('phase_'):].find('_')
    return path[:cut] + '/' + '/'.join(path[cut + 1:].split('_'))


def _bind(np_, anim_file):
    """Binds anim_file to the one Character under np_; returns (control, bundle)."""
    from panda3d.core import Character, Loader, Filename, PartGroup, AnimBundleNode
    chars = np_.findAllMatches('**/+Character')
    if chars.getNumPaths() != 1:
        raise ValueError(f'{np_.getName()}: expected one Character, found {chars.getNumPaths()}')
    bundle = chars.getPath(0).node().getBundle(0)
    anim_np = Loader.getGlobalPtr().loadSync(Filename.fromOsSpecific(str(anim_file)))
    if anim_np is None:
        raise ValueError(f'cannot load {anim_file}')
    from panda3d.core import NodePath
    anim_node = NodePath(anim_np).find('**/+AnimBundleNode')
    control = bundle.bindAnim(anim_node.node().getBundle(),
                              PartGroup.HMFOkPartExtra | PartGroup.HMFOkAnimExtra | PartGroup.HMFOkWrongRootName)
    if control is None:
        raise ValueError(f'{anim_file}: bind failed')
    return control, bundle, chars.getPath(0)


def _freeze_geometry(char_np):
    """Replaces the Character's skinned geometry with its current posed vertices (static)."""
    from panda3d.core import (GeomNode, PandaNode, Thread, GeomVertexFormat, GeomVertexAnimationSpec,
                              GeomVertexData, NodePath)
    thread = Thread.getCurrentThread()
    for gnp in char_np.findAllMatches('**/+GeomNode'):
        gn = gnp.node()
        for i in range(gn.getNumGeoms()):
            geom = gn.modifyGeom(i)
            vd = geom.getVertexData().animateVertices(True, thread)
            fmt = GeomVertexFormat(vd.getFormat())
            fmt.setAnimation(GeomVertexAnimationSpec())
            vd = GeomVertexData(vd.convertTo(GeomVertexFormat.registerFormat(fmt)))
            vd.clearTransformBlendTable()
            vd.clearSliderTable()
            geom.setVertexData(vd)
    plain = NodePath(PandaNode(char_np.getName()))
    plain.reparentTo(char_np.getParent())
    plain.setTransform(char_np.getTransform())
    plain.setState(char_np.getState())
    for child in list(char_np.getChildren()):
        child.reparentTo(plain)
    char_np.removeNode()


def freeze_interactive(resources, np_, zone_id, dna_anim):
    """Poses one interactive_prop node as InteractiveAnimatedProp.enter does; returns the anim."""
    code = np_.getTag('DNACode') or np_.getName()
    name = np_.getName()
    cls = next((c for k, c in _CLASSES.items() if k in name), None)
    hood = zone_id - zone_id % 1000
    anim = idle_tables(resources)[cls].get(hood) if cls else None
    anim = anim or dna_anim
    if not anim:
        return None
    path = Path(resources) / _model_dir(code) / (anim + '.bam')
    control, bundle, char_np = _bind(np_, path)
    control.pose(0)
    bundle.forceUpdate()
    _freeze_geometry(char_np)
    return anim


def _stem(code, anim):
    return (code.split('__')[-1] + '_' + anim.replace('tt_a_ara_', '')).replace('-', '_')


def _bake_bob(holder, out_dir, stem):
    """SleepingHydrantAnimatedProp.bobTask: node Sz = 1 + 0.08 sin(t), as a vertex loop."""
    import numpy as np
    import extract
    import tta
    import ttm
    ex = extract.Extractor(max_tex=128, encode_textures=True)
    ex.add(holder, animated=True)
    ref = ex.batches()
    stats = ttm.write(str(Path(out_dir) / f'{stem}.ttm'), ex.textures, ref)
    base = np.concatenate([b.verts[:, :3] for b in ref])
    frames = []
    for i in range(BOB_FRAMES):
        f = base.copy()
        f[:, 2] *= 1.0 + 0.08 * math.sin(2 * math.pi * i / BOB_FRAMES)
        frames.append(f)
    fps = BOB_FRAMES / (2 * math.pi)
    anim = tta.write(str(Path(out_dir) / f'{stem}.tta'), np.array(frames), fps)
    holder.removeNode()
    return {'model': stats, 'anim': anim, 'fps': fps, 'source_frames': BOB_FRAMES, 'source_fps': fps}


def _bake_kind(resources, model_np, anim_file, out_dir, stem):
    """model_np: a fresh copy of the prop model (prop-local). Writes <stem>.ttm / .tta."""
    import numpy as np
    import extract
    import tta
    import ttm
    from panda3d.core import NodePath
    holder = NodePath('anim_prop')
    model_np.reparentTo(holder)
    model_np.clearTransform()
    if anim_file is None:
        return _bake_bob(holder, out_dir, stem)
    control, bundle, _ = _bind(holder, anim_file)
    count, rate = control.getNumFrames(), control.getFrameRate()

    def capture(encode):
        ex = extract.Extractor(max_tex=128, encode_textures=encode)
        ex.add(holder, animated=True)
        return ex, ex.batches()
    control.pose(0)
    bundle.forceUpdate()
    ex, ref = capture(True)
    stats = ttm.write(str(Path(out_dir) / f'{stem}.ttm'), ex.textures, ref)
    cache = {}

    def at(i):
        if i not in cache:
            control.pose(i)
            bundle.forceUpdate()
            _, bs = capture(False)
            if [len(b.verts) for b in bs] != [len(b.verts) for b in ref]:
                raise RuntimeError(f'{stem}: vertex layout changed')
            cache[i] = np.concatenate([b.verts[:, :3] for b in bs])
        return cache[i]
    nv = sum(len(b.verts) for b in ref)
    fps = next((f for f in FPS_STEPS if max(1, math.ceil(count / rate * f - 1e-8)) * nv * 8 <= CLIP_BYTES),
               FPS_STEPS[-1])
    frames = []
    for i in range(max(1, math.ceil(count / rate * fps - 1e-8))):
        src = i * rate / fps
        a = min(int(src), count - 1)
        b = min(a + 1, count - 1)
        frames.append(at(a) * (1 - (src - a)) + at(b) * (src - a))
    anim = tta.write(str(Path(out_dir) / f'{stem}.tta'), np.array(frames), fps)
    holder.removeNode()
    return {'model': stats, 'anim': anim, 'fps': fps, 'source_frames': count, 'source_fps': rate}


HQ_PROPS = {'animated_prop_HQPeriscopeAnimatedProp': ('phase_3.5/models/props/HQ_periscope-chan', KIND_PERISCOPE, 'hq_periscope'),
            'animated_prop_HQTelescopeAnimatedProp': ('phase_3.5/models/props/HQ_telescope-chan', KIND_TELESCOPE, 'hq_telescope')}

# Other AnimatedProp classes this DNA uses, by class name: (anim, stem). PetShopFishAnimatedProp,
# the playground pet shop's window fish, loops 'swim' on enter. SleepingHydrantAnimatedProp (one
# Dreamland 9200 hydrant) has no anim: its bobTask sets the node's Sz to 1 + 0.08 sin(t), baked
# here as a 2 pi second vertex loop.
LOOP_PROPS = {'PetShopFishAnimatedProp': ('phase_4/models/props/exteriorfish-swim', 'petshop_fish'),
              'SleepingHydrantAnimatedProp': (None, 'sleeping_hydrant')}
BOB_FRAMES = 75  # over the 2 pi s bob period


def split(root, zone, store, resources, data_dir, write=True):
    """Stashes the zone's moving animated props; writes data/animprops/* and <zone>.tap lines.

    store: the zone's DNA storage (code -> model node). Returns the instance records.
    """
    from panda3d.core import NodePath
    out_dir = Path(data_dir) / 'animprops'
    records = []
    baked = set()
    nodes = [n for n in root.findAllMatches('**/animated_prop_*') if not n.isStashed()]
    for np_ in nodes:
        code = np_.getTag('DNACode') or np_.getName()
        if code.endswith('_DNARoot'):
            code = code[:-len('_DNARoot')]
        # TownLoader.createAnimatedProps picks the class from the node name.
        name = np_.getName()
        cls = ('GenericAnimatedProp' if name.startswith('animated_prop_generic')
               else name[len('animated_prop_'):].split('_')[0])
        if cls == 'GenericAnimatedProp':
            anim = np_.getTag('DNAAnim')
            anim_file = Path(resources) / _model_dir(code) / (anim + '.bam')
            kind = KIND_LOOP
        elif code in HQ_PROPS:
            anim_file = Path(resources) / (HQ_PROPS[code][0] + '.bam')
            anim = Path(HQ_PROPS[code][0]).name
            kind = HQ_PROPS[code][1]
        elif cls in LOOP_PROPS:
            anim_file = (Path(resources) / (LOOP_PROPS[cls][0] + '.bam')) if LOOP_PROPS[cls][0] else None
            anim = LOOP_PROPS[cls][1]
            kind = KIND_LOOP
        else:
            raise ValueError(f'{zone}: unhandled animated prop {name}')
        stem = (HQ_PROPS[code][2] if code in HQ_PROPS else LOOP_PROPS[cls][1] if cls in LOOP_PROPS
                else _stem(code, anim))
        mat = np_.getMat(root)
        if write and stem not in baked:
            baked.add(stem)
            out_dir.mkdir(parents=True, exist_ok=True)
            src = store.find_node(code)
            if src is None:
                raise ValueError(f'{zone}: no storage node for {code}')
            _bake_kind(resources, src.copyTo(NodePath('tmp')), anim_file, out_dir, stem)
        m = [mat.getCell(r, c) for c in range(3) for r in range(4)]
        records.append((stem, kind, m))
        # Only the drawn geometry leaves the static bake; the prop's collision stays.
        for gnp in np_.findAllMatches('**/+GeomNode'):
            gnp.stash()
    return records


def write_tap(path, records):
    with open(path, 'w') as f:
        for stem, kind, m in records:
            f.write(stem + ' ' + str(kind) + ' ' + ' '.join(f'{v:.6f}' for v in m) + '\n')


def freeze_all(root, zone, resources):
    """Poses every interactive_prop under root; returns {anim name: count}."""
    counts = {}
    for np_ in root.findAllMatches('**/interactive_prop_*'):
        if not np_.findAllMatches('**/+Character').getNumPaths():
            continue
        anim = freeze_interactive(resources, np_, zone, np_.getTag('DNAAnim'))
        if anim:
            counts[anim] = counts.get(anim, 0) + 1
    return counts
