#!/usr/bin/env python3
"""Platform badges: the third dc file must not move any stock class or field number.

    .venv/bin/python -I tools/dcgen/test_platform_dc.py [etc-dir]

Checks, with stock Panda's DC parser:
  1. otp.dc + toon.dc still hash to 547155168 (what CLIENT_HELLO sends).
  2. Reading platform.dc last leaves every existing class and field number unchanged;
     TTPlatformManager is class 398 with fields 2270-2273.
  3. dcgen's output with platform.dc equals the two-file output plus exactly the
     TTPlatformManager namespace (kHash unchanged), and that equals the committed
     source/net/dc_generated.h.
"""
import os
import subprocess
import sys

from panda3d.direct import DCFile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
PLATFORM_DC = os.path.join(ROOT, 'tools', 'server', 'platform', 'platform.dc')


def find_etc(argv):
    for c in argv[1:2] + [os.path.join(ROOT, 'ref', 'open-toontown', 'etc'),
                          os.path.join(ROOT, 'ref', 'ref', 'open-toontown', 'etc'),
                          '/workspace/toontown/claude/Toontown-3ds/ref/open-toontown/etc']:
        if c and os.path.exists(os.path.join(c, 'otp.dc')):
            return c
    sys.exit('no open-toontown etc dir (pass it as the first argument)')


def read(paths):
    dc = DCFile()
    for p in paths:
        assert dc.read(p), p
    return dc


def numbers(dc):
    out = {}
    for i in range(dc.getNumClasses()):
        c = dc.getClass(i)
        out[('class', c.getName())] = c.getNumber()
        for j in range(c.getNumFields()):
            f = c.getField(j)
            out[(c.getName(), f.getName())] = f.getNumber()
    return out


def dcgen(etc, *extra):
    return subprocess.run([sys.executable, '-I', os.path.join(HERE, 'dcgen.py'), etc, *extra],
                          check=True, capture_output=True, text=True).stdout


def main(argv):
    etc = find_etc(argv)
    stock = [os.path.join(etc, 'otp.dc'), os.path.join(etc, 'toon.dc')]
    fails = 0

    def check(ok, what):
        nonlocal fails
        print(('ok   ' if ok else 'FAIL ') + what)
        fails += not ok

    two = read(stock)
    three = read(stock + [PLATFORM_DC])
    check(two.getHash() == 547155168, f'stock hash {two.getHash()} == 547155168')
    check(three.getHash() != two.getHash(), 'three-file hash differs (so the client must pin it)')
    a, b = numbers(two), numbers(three)
    moved = [k for k in a if b.get(k) != a[k]]
    check(not moved, f'{len(a)} stock class/field numbers unchanged' + (f' (moved: {moved[:5]})' if moved else ''))
    new = {k: v for k, v in b.items() if k not in a}
    check(new == {('class', 'TTPlatformManager'): 398, ('TTPlatformManager', 'setMyPlatform'): 2270,
                  ('TTPlatformManager', 'requestPlatforms'): 2271, ('TTPlatformManager', 'setPlatforms'): 2272,
                  ('TTPlatformManager', 'clearPlatform'): 2273}, f'new numbers {sorted(new.values())}')
    check(max(a.values()) < 398 or all(v < 2270 for k, v in a.items() if k[0] != 'class'),
          'every stock field number is below 2270')

    with_p = dcgen(etc)
    without = dcgen(etc, '--no-platform')
    start = with_p.find('namespace TTPlatformManager {')
    end = with_p.find('} // namespace TTPlatformManager\n', start)
    check(start > 0 and end > start, 'generated header has the TTPlatformManager namespace')
    block = with_p[start:end + len('} // namespace TTPlatformManager\n\n')]
    check(with_p.replace(block, '') == without, 'generated tables identical to the two-file tables otherwise')
    check('constexpr uint32_t kHash = 547155168u;' in with_p, 'kHash stays 547155168')
    for line in ('kClassId = 398;', 'k_setMyPlatform = 2270;', 'k_requestPlatforms = 2271;',
                 'k_setPlatforms = 2272;', 'kw_setMyPlatform = 0x010;', 'kw_requestPlatforms = 0x010;',
                 'kw_setPlatforms = 0x000;'):
        check(line in block, line)
    committed = open(os.path.join(ROOT, 'source', 'net', 'dc_generated.h')).read()
    check(committed.rstrip('\n') == with_p.rstrip('\n'), 'source/net/dc_generated.h is up to date')
    print('FAILED' if fails else 'all DC numbering checks passed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
