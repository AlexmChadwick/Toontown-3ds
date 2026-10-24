#!/usr/bin/env python3
"""Bake every toon body part (legs, torso, head, eyelashes) as separate .ttm models
plus per-part vertex animations (.tta), a clothing texture pack (.ttx) and a manifest.

Parts are baked in their own space so any DNA combination works at runtime:
legs are drawn at the toon's transform, the torso at legs' joint_hips (the legs
clips' attach track), the head at the torso's def_head/joint_head (the torso clips'
attach track) and the female eyelashes at the head's transform.

    # everything (SD set) + the boot set (default dog/m/ms toon) for RomFS:
    .venv/bin/python -I tools/pipeline/bake_toon.py --out-dir sdmc-out/3ds/toontown3ds/data/toon \
        --boot-dir romfs/baked/toon
    # one combination only (legacy form, still used by bake_phase1.sh):
    .venv/bin/python -I tools/pipeline/bake_toon.py --out-dir romfs/baked/toon \
        --head dls --torso ms --legs m --lod 500

Files written (codes as in toontown/toon/ToonDNA.py):
    legs_<s|m|l>_<lod>.ttm             + legs_<code>_<lod>_<anim>.tta   (attach: torso root)
    torso_<ss|ms|ls|sd|md|ld>_<lod>.ttm + torso_<code>_<lod>_<anim>.tta  (attach: head root)
    head_<toonHeadType>_<lod>.ttm       + head_<code>_<lod>_<anim>.tta for dog heads only.
        Non-dog heads have no clips upstream (HeadAnimDict only has 'd' keys): they are
        static and get no .tta at all.
    lashes_<species><l|s>.ttm           female eyelashes ('open-long'/'open-short' of
        phase_3/models/char/<species>-lashes), static, drawn with the head's transform.
        Upstream only attaches them to the LOD 1000 head; we use them at every LOD.
    clothes.ttx                         clothing texture pack (format below)
    toon_manifest.json                  every file with sizes, vertex counts, etc.

Every part is baked once, independent of the other parts: a part's vertex frames are
in its own space and its attach track only depends on its own joints (the child part's
root is an identity node under the joint; asserted below for every child model).

DNA colors are not baked: tinted geometry gets a tint slot (TINT_* below, mirrored by
TintSlot in source/render/model.h) and the runtime supplies colors. Clothing textures
vary per DNA too: torso batches that upstream re-textures in Toon.generateToonClothes
carry a texture slot (ttm batch entry byte 'texSlot', TEX_SLOT_* below) and keep the
DNA index 0 texture as their own default; the runtime binds the DNA's texture from
clothes.ttx instead.

TTX ("toontown textures") v1, little endian, texture data 128-byte aligned:
    Header (32 bytes)
      char[4] magic "TTX1"; u16 version; u16 numTextures
      u16 numEntries[4]   -- DNA table sizes: Shirts, Sleeves, BoyShorts, GirlBottoms
      u32 mapOffset       -- u16[sum(numEntries)]: texture index for each DNA table entry,
                             tables in the order above; 0xFFFF = not in this pack
      u32 texTableOffset  -- numTextures x 48-byte texture entries, byte-identical to the
                             TTM texture entry (tools/pipeline/ttm.py); dataOffset counts
                             from the start of the file
      u32 reserved[2]
"""
import argparse
import concurrent.futures
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
from panda3d.core import NodePath, loadPrcFileData  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RESOURCES = os.path.join(ROOT, 'ref', 'resources')
UPSTREAM = os.path.join(ROOT, 'ref', 'open-toontown')
loadPrcFileData('', f'window-type none\naudio-library-name null\nnotify-level warning\n'
                    f'model-path {RESOURCES}\ndefault-model-extension .bam')
from direct.actor.Actor import Actor  # noqa: E402
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import extract  # noqa: E402
import tta  # noqa: E402
import ttm  # noqa: E402

sys.path.insert(0, UPSTREAM)
from toontown.toon import ToonDNA  # noqa: E402  (tables only; stock panda3d imports it fine)

CHAR = 'phase_3/models/char/'
SPECIES = {'d': 'dog', 'c': 'cat', 'h': 'horse', 'm': 'mouse', 'r': 'rabbit', 'f': 'duck',
           'p': 'monkey', 'b': 'bear', 's': 'pig'}
# Toon.py / ToonHead.py, want-new-anims branch (the default).
DOG_HEADS = {'dls': 'tt_a_chr_dgm_shorts_head_', 'dss': 'tt_a_chr_dgm_skirt_head_',
             'dsl': 'tt_a_chr_dgs_shorts_head_', 'dll': 'tt_a_chr_dgl_shorts_head_'}
TORSOS = {'ss': 'tt_a_chr_dgs_shorts_torso_', 'ms': 'tt_a_chr_dgm_shorts_torso_',
          'ls': 'tt_a_chr_dgl_shorts_torso_', 'sd': 'tt_a_chr_dgs_skirt_torso_',
          'md': 'tt_a_chr_dgm_skirt_torso_', 'ld': 'tt_a_chr_dgl_skirt_torso_'}
# Naked torsos ('s', 'm', 'l') map to dogXX_Naked-torso-1000, which ref/resources does not
# ship; the runtime substitutes the shorts torso (source/world/toon_dna.cpp).
LEGS = {'s': 'tt_a_chr_dgs_shorts_legs_', 'm': 'tt_a_chr_dgm_shorts_legs_', 'l': 'tt_a_chr_dgl_shorts_legs_'}


def head_prefix(head):
    return DOG_HEADS[head] if head[0] == 'd' else f'{SPECIES[head[0]]}-heads-'


# Runtime animation name -> upstream file suffix, phase (Toon.py Phase3/Phase3_5AnimList).
ANIMS = {'neutral': ('neutral', 3), 'run': ('run', 3), 'walk': ('walk', 3.5),
         'jump-idle': ('jump-zhang', 3.5), 'wave': ('wave', 3.5),
         'throw': ('pie-throw', 3.5), 'pushbutton': ('press-button', 3.5),
         'cringe': ('cringe', 3.5), 'conked': ('conked', 3.5),
         'sidestep-left': ('sidestep-left', 3.5), 'sidestep-right': ('jump-back-right', 3.5),
         'duck': ('duck', 3.5), 'victory': ('victory-dance', 3.5),
         'lose': ('lose', 5), 'sad-walk': ('losewalk', 4),
         'teleport': ('teleport', 3.5), 'book': ('book', 3.5)}
BAKE_FPS = 12.0
LODS = ('500', '250')

TINT_HEAD, TINT_ARM, TINT_GLOVE, TINT_LEG, TINT_SHIRT, TINT_SLEEVE, TINT_BOTTOM = range(1, 8)
TINT_RULES = [('head*', TINT_HEAD), ('arms', TINT_ARM), ('neck', TINT_ARM),
              ('hands', TINT_GLOVE), ('legs', TINT_LEG), ('feet', TINT_LEG),
              ('torso-top', TINT_SHIRT), ('sleeves', TINT_SLEEVE), ('torso-bot*', TINT_BOTTOM)]
# ToonHead.generateToonColor: these species also color '**/ear?-*' with the head color.
EAR_TINT_SPECIES = ('cat', 'rabbit', 'bear', 'mouse', 'pig')
# Upstream uses drawInFront('eyes*', 'head-front*') and pupils in front of eyes.
LAYER_RULES = [('*pupil*', 2), ('eyes*', 1)]

# Clothing texture slots (ttm batch entry 'texSlot'): which DNA texture replaces the
# batch's own texture (Toon.generateToonClothes: torso-top, sleeves, torso-bot).
TEX_SLOT_NONE, TEX_SLOT_SHIRT, TEX_SLOT_SLEEVE, TEX_SLOT_BOTTOM = range(4)
TINT_TEX_SLOT = {TINT_SHIRT: TEX_SLOT_SHIRT, TINT_SLEEVE: TEX_SLOT_SLEEVE, TINT_BOTTOM: TEX_SLOT_BOTTOM}
SLOT_NODES = {TEX_SLOT_SHIRT: 'torso-top', TEX_SLOT_SLEEVE: 'sleeves', TEX_SLOT_BOTTOM: 'torso-bot'}

# Boot set (RomFS): the local player's default toon before login.
BOOT = {'legs': 'm', 'torso': 'ms', 'head': 'dls', 'lod': '500'}
TTX_MAGIC, TTX_VERSION = b'TTX1', 1
MANIFEST_VERSION = 1


def phase_dir(phase):
    return f'phase_{phase:g}/models/char/'


def mat34(np_from, np_to):
    """3x4 row-major matrix that maps np_from's space into np_to's (column vectors)."""
    m = np_from.getMat(np_to)  # Panda: row vectors, translation in row 3
    return np.array([[m[0][0], m[1][0], m[2][0], m[3][0]],
                     [m[0][1], m[1][1], m[2][1], m[3][1]],
                     [m[0][2], m[1][2], m[2][2], m[3][2]]])


# --- head fix-ups: ports of ToonHead.__fixHead* / setupMuzzles / __fixEyes (copy=1) ---

def _remove_all(root, pattern):
    for np_ in root.findAllMatches(pattern):
        np_.removeNode()


def _remove(root, pattern):
    np_ = root.find(pattern)
    assert not np_.isEmpty(), pattern  # upstream would fail on a missing node too
    np_.removeNode()


def fix_head(head_np, head):
    animal = SPECIES[head[0]]
    style = head[1:]
    if animal == 'dog':
        pass  # separate model per dog head type: no fix-up
    elif style == 'll':
        _remove_all(head_np, '**/*short*')
    elif style == 'ss':
        _remove_all(head_np, '**/*long*')
    else:
        keep, drop = ('long', 'short') if style == 'ls' else ('short', 'long')
        if animal not in ('duck', 'horse'):
            _remove(head_np, f'**/ears-{keep}' if animal == 'rabbit' else f'**/ears-{drop}')
        if animal != 'rabbit':
            _remove(head_np, f'**/eyes-{drop}')
        _remove(head_np, f'**/joint_pupilL_{drop}')
        _remove(head_np, f'**/joint_pupilR_{drop}')
        _remove(head_np, f'**/head-{drop}')
        _remove(head_np, f'**/head-front-{drop}')
        # The muzzle is the head type's third letter, i.e. the opposite length of the head
        # here (upstream removes muzzle-long* from 'ls' heads), except for rabbits.
        _remove_all(head_np, f'**/muzzle-{drop}*' if animal == 'rabbit' else f'**/muzzle-{keep}*')
    # setupMuzzles: expression muzzles are hidden; non-dogs keep muzzle*neutral. (Dogs
    # also get dogXX-headMuzzles-<lod>, which only holds the hidden expression muzzles,
    # so it is not loaded.)
    for expr in ('surprise', 'angry', 'sad', 'smile', 'laugh'):
        _remove_all(head_np, f'**/muzzle*{expr}')
    # __fixEyes / enterEyelidsOpen: the open-eyes texture with alpha, no vertex color.
    eyes = head_np.find('**/eyes*')
    if not eyes.isEmpty():
        eyes.setColorOff()
        eyes.setTexture(loader.loadTexture('phase_3/maps/eyes.jpg', 'phase_3/maps/eyes_a.rgb'), 1)  # noqa: F821


def head_tint_rules(head):
    if SPECIES[head[0]] in EAR_TINT_SPECIES:
        return TINT_RULES + [('ear?-*', TINT_HEAD)]
    return TINT_RULES


# --- extraction ---

def extract_part(part, skip, tint_rules, max_tex, encode=True):
    ex = extract.Extractor(max_tex=max_tex, tint_rules=tint_rules, layer_rules=LAYER_RULES,
                           encode_textures=encode)
    ex.add(part, animated=True, skip_under=skip)
    return ex


def read_batch_slots(path):
    buf = open(path, 'rb').read()
    nbatch = struct.unpack_from('<H', buf, 10)[0]
    batch_off = struct.unpack_from('<I', buf, 48)[0]  # header: ... texTableOffset@44, batchTableOffset@48
    return [buf[batch_off + 24 * i + 6] for i in range(nbatch)]


def write_ttm(path, ex, tex_slots=False):
    batches = ex.batches()
    if tex_slots:
        for b in batches:
            b.tex_slot = TINT_TEX_SLOT.get(b.tint, TEX_SLOT_NONE)
            if b.tex_slot:
                # The batch's own texture must be the DNA index 0 default of that slot.
                name = ex.textures[b.texture].name
                assert name.startswith(('desat_shirt', 'desat_sleeve', 'desat_shorts', 'desat_skirt')), name
    stats = ttm.write(path, ex.textures, batches)
    nverts = sum(len(b.verts) for b in batches)
    # Animation frames must line up with .ttm vertex order; ttm.write keeps the order
    # unless it had to split or unweld, which toon parts never need.
    assert stats['vertices'] == nverts, (path, stats['vertices'], nverts)
    slots = read_batch_slots(path)
    assert slots == [getattr(b, 'tex_slot', 0) for b in batches], (path, slots)
    stats['tex_slots'] = {str(s): int(n) for s, n in zip(*np.unique(slots, return_counts=True)) if s}
    return stats, nverts


def bake_animated(kind, code, lod, model_path, anim_paths, child_joint, out_dir, max_tex,
                  tint_rules, prepare=None, tex_slots=False):
    """Bakes one animated part (+ clips). child_joint: joint the next part hangs off."""
    report = {}
    actor = Actor({kind: model_path}, {kind: anim_paths or {}})
    part = actor.getPart(kind)
    assert part.getTransform().isIdentity(), model_path
    for name in ('boots_short', 'boots_long', 'shoes'):
        actor.findAllMatches(f'**/{name}').stash()
    if prepare:
        prepare(part)
    child = None
    if child_joint:
        joints = [j for j in child_joint if not part.find('**/' + j).isEmpty()]
        assert joints, (model_path, child_joint)
        # Actor.attach reparents the child part (identity root) under this node.
        child = part.find('**/' + joints[0]).attachNewNode('child_root')
    skip = [child] if child else []

    if anim_paths:  # geometry in the neutral pose, frame 0
        actor.pose('neutral' if 'neutral' in anim_paths else next(iter(anim_paths)), 0)
    actor.update(force=True)
    ex = extract_part(part, skip, tint_rules, max_tex)
    name = f'{kind}_{code}_{lod}'
    path = os.path.join(out_dir, name + '.ttm')
    stats, nverts = write_ttm(path, ex, tex_slots)
    report[os.path.basename(path)] = stats

    clips = {}
    for a in anim_paths or {}:
        nframes = actor.getNumFrames(a)
        src_fps = actor.getFrameRate(a)
        step = max(1, round(src_fps / BAKE_FPS))
        frames, attach = [], []
        for f in range(0, nframes, step):
            actor.pose(a, f)
            actor.update(force=True)
            fe = extract_part(part, skip, tint_rules, max_tex, encode=False)
            pos = np.concatenate([b.verts[:, :3] for b in fe.batches()])
            assert len(pos) == nverts, (name, a, len(pos), nverts)
            frames.append(pos)
            if child:
                attach.append(mat34(child, part))
        out = os.path.join(out_dir, f'{name}_{a}.tta')
        report[os.path.basename(out)] = tta.write(out, frames, src_fps / step, attach if child else None)
        clips[a] = os.path.basename(out)
    actor.cleanup()
    actor.removeNode()
    return report, {'ttm': os.path.basename(path), 'vertices': nverts, 'clips': clips}


def bake_legs(code, lod, anims, out_dir, max_tex):
    return bake_animated('legs', code, lod, CHAR + LEGS[code] + lod,
                         {a: phase_dir(ANIMS[a][1]) + LEGS[code] + ANIMS[a][0] for a in anims},
                         ('joint_hips',), out_dir, max_tex, TINT_RULES)


def bake_torso(code, lod, anims, out_dir, max_tex):
    def prepare(part):
        # Upstream darkens 'torso-bot-cap' (bottom color * 0.5); no LOD 500/250 torso has one.
        assert part.find('**/torso-bot-cap').isEmpty(), code
        for slot, node in SLOT_NODES.items():
            assert not part.find('**/' + node).isEmpty(), (code, node)
    return bake_animated('torso', code, lod, CHAR + TORSOS[code] + lod,
                         {a: phase_dir(ANIMS[a][1]) + TORSOS[code] + ANIMS[a][0] for a in anims},
                         ('def_head', 'joint_head'), out_dir, max_tex, TINT_RULES, prepare, tex_slots=True)


def bake_head(code, lod, anims, out_dir, max_tex):
    prefix = head_prefix(code)
    anim_paths = ({a: phase_dir(ANIMS[a][1]) + prefix + ANIMS[a][0] for a in anims}
                  if code[0] == 'd' else None)
    report, info = bake_animated('head', code, lod, CHAR + prefix + lod, anim_paths, None, out_dir,
                                 max_tex, head_tint_rules(code), lambda part: fix_head(part, code))
    info['species'] = SPECIES[code[0]]
    info['animated'] = bool(anim_paths)
    info['lashes'] = f'lashes_{code[0]}{code[1]}.ttm'
    return report, info


def bake_lashes(species_length, out_dir, max_tex):
    sp, length = species_length
    model = loader.loadModel(f'{CHAR}{SPECIES[sp]}-lashes')  # noqa: F821
    holder = NodePath('lashes')
    model.find('**/open-' + ('long' if length == 'l' else 'short')).copyTo(holder)
    ex = extract_part(holder, [], TINT_RULES, max_tex)
    path = os.path.join(out_dir, f'lashes_{sp}{length}.ttm')
    stats, nverts = write_ttm(path, ex)
    model.removeNode()
    return {os.path.basename(path): stats}, {'ttm': os.path.basename(path), 'vertices': nverts}


# --- clothing texture pack ---

def clothing_tables():
    return [('shirts', list(ToonDNA.Shirts)), ('sleeves', list(ToonDNA.Sleeves)),
            ('boy_shorts', list(ToonDNA.BoyShorts)), ('girl_bottoms', [b[0] for b in ToonDNA.GirlBottoms])]


def mat_subset():
    """DNA indices offered by Make-A-Toon (ToonDNA.TailorCollections[MAKE_A_TOON])."""
    shirts = sorted(set(ToonDNA.MakeAToonBoyShirts + ToonDNA.MakeAToonGirlShirts))
    sleeves = sorted({ToonDNA.ShirtStyles[s][1] for c in ToonDNA.TailorCollections[ToonDNA.MAKE_A_TOON][:2]
                      for s in c})
    return [set(shirts) | {0}, set(sleeves) | {0}, set(ToonDNA.MakeAToonBoyBottoms) | {0},
            set(ToonDNA.MakeAToonGirlBottoms) | {0}]


def write_ttx(path, max_tex, subset=None):
    textures, index_of, maps, missing = [], {}, [], []
    for t, (table, paths) in enumerate(clothing_tables()):
        m = []
        for i, p in enumerate(paths):
            if subset is not None and i not in subset[t]:
                m.append(0xFFFF)
                continue
            if p not in index_of:
                tex = loader.loadTexture(p, okMissing=True)  # noqa: F821
                if tex is None:
                    # Toon.generateToonClothes falls back to the table's entry 0.
                    missing.append(f'{table}[{i}] {p}')
                    m.append(m[0])
                    continue
                px, ncomp, pfmt = extract.texture_pixels(tex, max_tex)
                fmt = extract.choose_format(px, ncomp, pfmt)
                index_of[p] = len(textures)
                textures.append(ttm.Texture(os.path.basename(p), px, fmt,
                                            extract._WRAP.get(tex.getWrapU(), ttm.GPU_REPEAT),
                                            extract._WRAP.get(tex.getWrapV(), ttm.GPU_REPEAT)))
            m.append(index_of[p])
        maps.append(m)
    counts = [len(m) for m in maps]
    map_off = 32
    tex_off = ttm._align(map_off + 2 * sum(counts), 4)
    cursor = ttm._align(tex_off + 48 * len(textures))
    entries, blobs = [], []
    for t in textures:
        entries.append(struct.pack('<HHBBBBII32s', t.w, t.h, t.fmt, t.mips, t.wrap_s, t.wrap_t,
                                   cursor, len(t.data), t.name.encode()[:31]))
        blobs.append((cursor, t.data))
        cursor = ttm._align(cursor + len(t.data))
    out = bytearray(cursor)
    out[0:32] = struct.pack('<4sHH4HII2I', TTX_MAGIC, TTX_VERSION, len(textures), *counts,
                            map_off, tex_off, 0, 0)
    mb = np.array([i for m in maps for i in m], dtype='<u2').tobytes()
    out[map_off:map_off + len(mb)] = mb
    for i, e in enumerate(entries):
        out[tex_off + 48 * i: tex_off + 48 * (i + 1)] = e
    for off, data in blobs:
        out[off:off + len(data)] = data
    with open(path, 'wb') as f:
        f.write(out)
    formats = {}
    for t in textures:
        formats[ttm.FORMAT_NAMES[t.fmt]] = formats.get(ttm.FORMAT_NAMES[t.fmt], 0) + 1
    return {'file_bytes': len(out), 'textures': len(textures), 'entries': dict(zip(
        [n for n, _ in clothing_tables()], counts)), 'formats': formats,
        'sizes': sorted({f'{t.w}x{t.h}' for t in textures}), 'missing_fallback_to_0': missing}


# --- driver ---

def build_manifest(out_dir, parts, extra, lods, anims):
    files = {}
    for name in sorted(os.listdir(out_dir)):
        if name.endswith(('.ttm', '.tta', '.ttx')):
            files[name] = os.path.getsize(os.path.join(out_dir, name))
    by_ext = {}
    for name, size in files.items():
        ext = name.rsplit('.', 1)[1]
        by_ext[ext] = by_ext.get(ext, 0) + size
    return {'version': MANIFEST_VERSION, 'lods': list(lods), 'anims': list(anims), 'bake_fps': BAKE_FPS,
            'body_scale': otp_global('toonBodyScales'),
            'parts': parts, **extra, 'files': files,
            'totals': {'files': len(files), 'bytes': sum(files.values()), 'bytes_by_type': by_ext}}


def otp_global(name):
    """A literal from otp/otpbase/OTPGlobals.py (the module itself needs panda3d.otp)."""
    import ast
    tree = ast.parse(open(os.path.join(UPSTREAM, 'otp', 'otpbase', 'OTPGlobals.py')).read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise KeyError(name)


def csv(s, allowed):
    items = [x for x in s.split(',') if x]
    bad = [x for x in items if x not in allowed]
    if bad:
        raise SystemExit(f'unknown codes {bad}; allowed: {list(allowed)}')
    return items


def worker(args):
    """Bakes one job in this (fresh) process and prints its manifest fragment as JSON.

    Every job runs in its own process: in one long-lived process Panda's skinning
    results drift by a few float32 ULPs depending on what was loaded before, which
    flips int16 quantization of a few vertices and breaks byte-identical output.
    """
    ShowBase()
    kind, code = args.worker
    lods, anims = args.lods.split(','), [a for a in args.anims.split(',') if a]
    out = {}
    if kind in ('legs', 'torso', 'head'):
        bake = {'legs': bake_legs, 'torso': bake_torso, 'head': bake_head}[kind]
        for lod in lods:
            _, out[lod] = bake(code, lod, anims, args.out_dir, args.max_tex)
    elif kind == 'lashes':
        _, out = bake_lashes(code, args.out_dir, args.max_tex)
    elif kind == 'clothes':
        out = write_ttx(os.path.join(args.out_dir, 'clothes.ttx'), args.clothes_max_tex,
                        mat_subset() if code == 'mat' else None)
    print('\n' + json.dumps(out, sort_keys=True))


def run_jobs(args, jobs):
    """Runs (kind, code, out_dir) jobs as parallel worker processes; returns their results."""
    def one(job):
        kind, code, out_dir = job
        cmd = [sys.executable, '-I', os.path.abspath(__file__), '--worker', kind, code, '--out-dir', out_dir,
               '--lods', args.lods, '--anims', args.anims, '--max-tex', str(args.max_tex),
               '--clothes-max-tex', str(args.clothes_max_tex)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'{kind} {code} failed:\n{r.stdout[-2000:]}{r.stderr[-4000:]}')
        return json.loads(r.stdout.strip().splitlines()[-1])
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        return list(pool.map(one, jobs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out-dir', required=True)
    ap.add_argument('--boot-dir', help='also write the boot set (dog/m/ms LOD 500) here')
    ap.add_argument('--heads', '--head', default=','.join(ToonDNA.toonHeadTypes))
    ap.add_argument('--torsos', '--torso', default=','.join(TORSOS))
    ap.add_argument('--legs', default=','.join(LEGS))
    ap.add_argument('--lods', '--lod', default=','.join(LODS))
    ap.add_argument('--anims', default=','.join(ANIMS))
    ap.add_argument('--battle-only', action='store_true',
                    help='battle clips plus neutral as the unchanged base-mesh pose')
    ap.add_argument('--max-tex', type=int, default=128)
    ap.add_argument('--clothes-max-tex', type=int, default=64,
                    help='clothing textures are up to 128x128 upstream; 64 keeps clothes.ttx ~1/4 the size')
    ap.add_argument('--clothes', choices=('all', 'mat', 'none'), default='all',
                    help='clothes.ttx content: every DNA texture, the Make-A-Toon subset, or none')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--worker', nargs=2, metavar=('KIND', 'CODE'), help=argparse.SUPPRESS)
    args = ap.parse_args()
    if args.battle_only:
        args.anims = ','.join(a for a in ANIMS if a not in ('run', 'walk', 'jump-idle', 'wave'))
    if args.worker:
        return worker(args)
    heads = csv(args.heads, ToonDNA.toonHeadTypes)
    torsos = csv(args.torsos, TORSOS)
    legs = csv(args.legs, LEGS)
    lods = csv(args.lods, ('1000', '500', '250'))
    anims = csv(args.anims, ANIMS)
    os.makedirs(args.out_dir, exist_ok=True)

    lashes = sorted({h[:2] for h in heads})
    jobs = ([('legs', c, args.out_dir) for c in legs] + [('torso', c, args.out_dir) for c in torsos] +
            [('head', c, args.out_dir) for c in heads] + [('lashes', c, args.out_dir) for c in lashes])
    if args.clothes != 'none':
        jobs.append(('clothes', args.clothes, args.out_dir))
    if args.boot_dir:
        os.makedirs(args.boot_dir, exist_ok=True)
        for name in os.listdir(args.boot_dir):
            if name.endswith(('.ttm', '.tta', '.ttx', '.json')):
                os.remove(os.path.join(args.boot_dir, name))
        jobs.append(('clothes', 'mat', args.boot_dir))
    results = dict(zip([(k, c, d) for k, c, d in jobs], run_jobs(args, jobs)))

    parts = {'legs': {}, 'torso': {}, 'head': {}, 'lashes': {}}
    extra = {}
    for (kind, code, out_dir), res in results.items():
        if kind == 'clothes':
            if out_dir == args.out_dir:
                extra['clothes'] = {'file': 'clothes.ttx', **res}
        else:
            parts[kind][code] = res
    manifest = build_manifest(args.out_dir, parts, extra, lods, anims)
    with open(os.path.join(args.out_dir, 'toon_manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    if args.boot_dir:
        bake_boot(args, anims, results[('clothes', 'mat', args.boot_dir)])
    print(json.dumps({'totals': manifest['totals'], 'clothes': extra.get('clothes')}, indent=1))


def bake_boot(args, anims, clothes):
    """Copies the boot toon's parts into boot_dir (its Make-A-Toon clothes.ttx is already
    there) and writes its manifest."""
    with open(os.path.join(args.out_dir, 'toon_manifest.json')) as f:
        full = json.load(f)
    lod = BOOT['lod']
    parts = {}
    for kind in ('legs', 'torso', 'head'):
        info = full['parts'][kind][BOOT[kind]][lod]
        parts[kind] = {BOOT[kind]: {lod: info}}
        for name in [info['ttm']] + list(info['clips'].values()):
            shutil.copyfile(os.path.join(args.out_dir, name), os.path.join(args.boot_dir, name))
    parts['lashes'] = {}
    manifest = build_manifest(args.boot_dir, parts, {'clothes': {'file': 'clothes.ttx', **clothes}}, [lod], anims)
    with open(os.path.join(args.boot_dir, 'toon_manifest.json'), 'w') as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
    print('boot set:', json.dumps(manifest['totals']))


if __name__ == '__main__':
    main()
