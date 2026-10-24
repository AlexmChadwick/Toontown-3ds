#!/usr/bin/env python3
"""Host test: source/world/toon_dna.cpp against the upstream Python (ToonDNA.py).

    .venv/bin/python -I tools/toon_dna_test/run_test.py [--count 20000] [--seed 1]

Builds tools/toon_dna_test/toon_dna_test.cpp + source/world/toon_dna.cpp with the host
c++ (warnings are errors), then compares, for many random netstrings (valid, mutated,
wrong length, wrong type), the C++ isValidNetString/parse result with upstream
ToonDNA.isValidNetString/makeFromNetString, and the derived part codes, tints, clothing
textures, body scale and height with a reference written from Toon.py/ToonHead.py. Also
checks every Make-A-Toon build against the server rule. Exit status 1 on any mismatch.
"""
import argparse
import ast
import builtins
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
UPSTREAM = os.path.join(ROOT, 'ref', 'ref', 'open-toontown') if os.path.isdir(os.path.join(ROOT, 'ref', 'ref', 'open-toontown')) else os.path.join(ROOT, 'ref', 'open-toontown')
sys.path.insert(0, UPSTREAM)

from toontown.toon import ToonDNA as T  # noqa: E402

# isValidNetString uses `choice`, a builtin that upstream's ShowBase/PythonUtil installs.
builtins.choice = lambda cond, a, b: a if cond else b


def otp_global(name):
    tree = ast.parse(open(os.path.join(UPSTREAM, 'otp', 'otpbase', 'OTPGlobals.py')).read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(getattr(t, 'id', None) == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise KeyError(name)


BODY_SCALES = otp_global('toonBodyScales')
LEG_H, TORSO_H, HEAD_H = otp_global('legHeightDict'), otp_global('torsoHeightDict'), otp_global('headHeightDict')


def build_driver(tmp):
    exe = os.path.join(tmp, 'toon_dna_test')
    cmd = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
           '-I', os.path.join(ROOT, 'source'), os.path.join(HERE, 'toon_dna_test.cpp'),
           os.path.join(ROOT, 'source', 'world', 'toon_dna.cpp'), '-o', exe]
    subprocess.run(cmd, check=True)
    return exe


def run(exe, lines):
    out = subprocess.run([exe], input='\n'.join(lines) + '\n', capture_output=True, text=True, check=True).stdout
    return out.splitlines()


def upstream_valid(b):
    try:
        return bool(T.ToonDNA().isValidNetString(b))
    except Exception:  # noqa: BLE001  (upstream raises on some garbage; the server treats it as a failure)
        return False


def reference(b):
    """Expected 'valid ...' fields from the upstream DNA."""
    d = T.ToonDNA()
    d.makeFromNetString(b)
    fields = [T.toonHeadTypes.index(d.head), T.toonTorsoTypes.index(d.torso), T.toonLegTypes.index(d.legs),
              1 if d.gender == 'm' else 0, d.topTex, d.topTexColor, d.sleeveTex, d.sleeveTexColor,
              d.botTex, d.botTexColor, d.armColor, d.gloveColor, d.legColor, d.headColor]
    # Toon.generateToonClothes: a girl's torso follows her bottom; naked -> shorts (ours).
    torso = d.torso if len(d.torso) == 2 else d.torso + 's'
    if d.gender == 'f':
        torso = torso[0] + ('d' if T.GirlBottoms[d.botTex][1] == T.SKIRT else 's')
    animal = d.getAnimal()
    scale = BODY_SCALES[animal]
    height = LEG_H[d.legs] * scale + TORSO_H[d.torso] * scale + HEAD_H[d.head] * 1.0
    tints = [(1, 1, 1, 1), d.getHeadColor(), d.getArmColor(), d.getGloveColor(), d.getLegColor(),
             T.ClothesColors[d.topTexColor], T.ClothesColors[d.sleeveTexColor], T.ClothesColors[d.botTexColor]]
    clothes = [(0, d.topTex), (1, d.sleeveTex), (2 if d.gender == 'm' else 3, d.botTex)]
    return {'fields': fields, 'legs': d.legs, 'torso': torso, 'head': d.head,
            'lashes': d.head[:2] if d.gender == 'f' else '-', 'anim': 1 if animal == 'dog' else 0,
            'scale': scale, 'height': height, 'tints': [float(c[i]) for c in tints for i in range(4)],
            'clothes': clothes, 'roundtrip': d.makeNetString().hex(), 'species': animal}


def check_valid_line(line, ref):
    p = line.split()
    errs = []
    if [int(x) for x in p[1:15]] != ref['fields']:
        errs.append(('fields', p[1:15], ref['fields']))
    legs, torso, head, lashes, anim = p[15:20]
    for k, v in (('legs', legs), ('torso', torso), ('head', head), ('lashes', lashes)):
        if v != ref[k]:
            errs.append((k, v, ref[k]))
    if int(anim) != ref['anim']:
        errs.append(('anim', anim, ref['anim']))
    if abs(float(p[20]) - ref['scale']) > 1e-6 or abs(float(p[21]) - ref['height']) > 1e-5:
        errs.append(('scale/height', p[20:22], ref['scale'], ref['height']))
    tints = [float(x) for x in p[22:54]]
    if max(abs(a - b) for a, b in zip(tints, ref['tints'])) > 1e-6:
        errs.append(('tints', tints, ref['tints']))
    clothes = [tuple(int(v) for v in x.split(':')) for x in p[54:57]]
    if clothes != ref['clothes']:
        errs.append(('clothes', clothes, ref['clothes']))
    if p[57] != ref['roundtrip']:
        errs.append(('roundtrip', p[57], ref['roundtrip']))
    if p[58] != ref['species']:
        errs.append(('species', p[58], ref['species']))
    return errs


def random_valid(rng):
    gender = rng.choice([0, 1, 1, rng.randrange(2, 256)])
    male = gender == 1
    return bytes([ord('t'), rng.randrange(len(T.toonHeadTypes)), rng.randrange(len(T.toonTorsoTypes)),
                  rng.randrange(len(T.toonLegTypes)), gender, rng.randrange(len(T.Shirts)),
                  rng.randrange(len(T.ClothesColors)), rng.randrange(len(T.Sleeves)),
                  rng.randrange(len(T.ClothesColors)),
                  rng.randrange(len(T.BoyShorts) if male else len(T.GirlBottoms)),
                  rng.randrange(len(T.ClothesColors)), rng.randrange(len(T.allColorsList)), 0,
                  rng.randrange(len(T.allColorsList)), rng.randrange(len(T.allColorsList))])


def netstring_cases(rng, count):
    cases = []
    for _ in range(count):
        cases.append(random_valid(rng))
    for _ in range(count):  # one field mutated: about half end up invalid
        b = bytearray(random_valid(rng))
        i = rng.randrange(1, 15)
        b[i] = rng.choice([rng.randrange(256), rng.randrange(70), b[i] + 1 & 255])
        cases.append(bytes(b))
    for _ in range(count // 10):
        b = bytearray(random_valid(rng))
        b[0] = rng.choice(b'uTs0 ')
        cases.append(bytes(b))
        n = rng.choice([0, 1, 14, 16, 30])
        cases.append((bytes(b) * 3)[:n])
    # Boundary values of every field.
    base = T.ToonDNA()
    base.newToon(('dls', 'ms', 'm', 'm'))
    for gender in ('m', 'f'):
        base.gender = gender
        b0 = bytearray(base.makeNetString())
        limits = [len(T.toonHeadTypes), len(T.toonTorsoTypes), len(T.toonLegTypes), 256, len(T.Shirts),
                  len(T.ClothesColors), len(T.Sleeves), len(T.ClothesColors),
                  len(T.BoyShorts) if gender == 'm' else len(T.GirlBottoms), len(T.ClothesColors),
                  len(T.allColorsList), 1, len(T.allColorsList), len(T.allColorsList)]
        for i, lim in enumerate(limits, start=1):
            for v in {lim - 1, min(lim, 255), 255, 0}:
                b = bytearray(b0)
                b[i] = v
                cases.append(bytes(b))
    # Upstream's own random toons (Make-A-Toon style DNAs).
    for k in range(count // 10):
        d = T.ToonDNA()
        d.newToonRandom(seed=k, gender=rng.choice('mf'))
        cases.append(d.makeNetString())
    return cases


def mat_tables():
    mat = T.TailorCollections[T.MAKE_A_TOON]
    shirts = {}
    bottoms = {}
    for g, si, bi in ((1, T.BOY_SHIRTS, T.BOY_SHORTS), (0, T.GIRL_SHIRTS, T.GIRL_BOTTOMS)):
        shirts[g] = [(T.ShirtStyles[s][0], T.ShirtStyles[s][1], c[0], c[1]) for s in mat[si] for c in T.ShirtStyles[s][2]]
        bottoms[g] = [(T.BottomStyles[s][0], c) for s in mat[bi] for c in T.BottomStyles[s][1]]
    colors = {1: list(T.defaultBoyColorList), 0: list(T.defaultGirlColorList)}
    return shirts, bottoms, colors


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--count', type=int, default=20000)
    ap.add_argument('--seed', type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        exe = build_driver(tmp)

        # 1. Netstrings.
        cases = netstring_cases(rng, args.count)
        out = run(exe, ['parse ' + c.hex() for c in cases])
        assert len(out) == len(cases)
        nvalid = 0
        for c, line in zip(cases, out):
            want = upstream_valid(c)
            got = line.split()[0]
            if got not in ('valid', 'invalid') or (got == 'valid') != want:
                failures.append(('validity', c.hex(), line[:40], want))
                continue
            if want:
                nvalid += 1
                errs = check_valid_line(line, reference(c))
                if errs:
                    failures.append(('decode', c.hex(), errs[:2]))
        print(f'netstrings: {len(cases)} cases, {nvalid} valid, {len(cases) - nvalid} invalid')

        # 2. Default toon.
        d = T.ToonDNA()
        d.newToon(('dls', 'ms', 'm', 'm'))
        got = run(exe, ['default'])[0]
        if got != d.makeNetString().hex() or not upstream_valid(bytes.fromhex(got)):
            failures.append(('default', got, d.makeNetString().hex()))
        print('default toon:', got)

        # 3. Make-A-Toon tables and builds.
        shirts, bottoms, colors = mat_tables()
        mat_out = run(exe, ['mat'])
        want_mat = []
        for gender in (0, 1):  # Gender::Female = 0 first, as the driver prints
            g = gender
            want_mat.append(f'colors {g} ' + ' '.join(map(str, colors[gender])))
            want_mat.append(f'shirts {g} ' + ' '.join('%d,%d,%d,%d' % s for s in shirts[gender]))
            want_mat.append(f'bottoms {g} ' + ' '.join(
                '%d,%d,%d' % (b, c, 1 if gender == 0 and T.GirlBottoms[b][1] == T.SKIRT else 0)
                for b, c in bottoms[gender]))
        want_mat.append('heads ' + ' '.join(h for s in T.toonSpeciesTypes for h in T.getHeadList(s)))
        want_mat.append('end')
        if mat_out != want_mat:
            failures.append(('mat tables', mat_out[:3], want_mat[:3]))

        cmds, expect = [], []
        for si, sp in enumerate(T.toonSpeciesTypes):
            heads = T.getHeadList(sp)
            for vi, head in enumerate(heads):
                for ts in range(3):
                    for ls in range(3):
                        for g in (0, 1):
                            for shi, sh in enumerate(shirts[g]):
                                bi = rng.randrange(len(bottoms[g]))
                                bot = bottoms[g][bi]
                                hc, ac, lc = (rng.choice(colors[g]) for _ in range(3))
                                cmds.append(f'build {si} {vi} {ts} {ls} {g} {hc} {ac} {lc} {shi} {bi}')
                                skirt = g == 0 and T.GirlBottoms[bot[0]][1] == T.SKIRT
                                dd = T.ToonDNA()
                                dd.newToonFromProperties(head, 'sml'[ts] + ('d' if skirt else 's'), 'sml'[ls],
                                                         'm' if g else 'f', ac, 0, lc, hc,
                                                         sh[0], sh[2], sh[1], sh[3], bot[0], bot[1])
                                expect.append('ok ' + dd.makeNetString().hex())
        # Rejections: outside the Make-A-Toon options.
        bad = ['build 9 0 1 1 1 1 1 1 0 0', 'build 3 2 1 1 1 1 1 1 0 0', 'build 0 4 1 1 1 1 1 1 0 0',
               'build 0 0 3 1 1 1 1 1 0 0', 'build 0 0 1 3 1 1 1 1 0 0', 'build 0 0 1 1 2 1 1 1 0 0',
               'build 0 0 1 1 1 0 1 1 0 0', 'build 0 0 1 1 1 1 25 1 0 0', 'build 0 0 1 1 1 1 1 26 0 0',
               f'build 0 0 1 1 1 1 1 1 {len(shirts[1])} 0', f'build 0 0 1 1 0 1 1 1 0 {len(bottoms[0])}']
        cmds += bad
        expect += ['reject'] * len(bad)
        out = run(exe, cmds)
        nbuild = 0
        for cmd, line, want in zip(cmds, out, expect):
            if line != want:
                failures.append(('build', cmd, line, want))
            elif want != 'reject':
                nbuild += 1
                if not upstream_valid(bytes.fromhex(line.split()[1])):
                    failures.append(('build not server-valid', cmd, line))
        print(f'make-a-toon: {nbuild} builds checked against upstream + isValidNetString, {len(bad)} rejections')

    for f in failures[:20]:
        print('FAIL', f)
    print('FAILED' if failures else 'PASSED', f'({len(failures)} failures)')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
