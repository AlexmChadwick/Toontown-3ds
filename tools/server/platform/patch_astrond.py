#!/usr/bin/env python3
"""Patch an astrond.yml for platform badges (idempotent, text level, keeps comments).

    python3 -I patch_astrond.py IN.yml OUT.yml --platform-dc /abs/path/platform.dc

1. dc_files: append platform.dc after the toon.dc line (so it loads LAST).
2. uberdogs: add {class: TTPlatformManager, id: 4721, anonymous: false}.
3. clientagent role: manual_dc_hash: 547155168, so the HELLO hash stays the 2-file one.
Fails (exit 2) if an anchor is missing instead of writing a half-patched file.
"""
import argparse
import re
import sys

DO_ID = 4721
DC_HASH = 547155168


def patch(text, platform_dc):
    lines = text.splitlines()
    out = []
    if 'class: TTPlatformManager' in text and 'manual_dc_hash' in text and 'platform.dc' in text:
        return text if text.endswith('\n') else text + '\n'
    if re.search(r'\bid:\s*%d\b' % DO_ID, text):
        raise ValueError('doId %d already used in astrond.yml' % DO_ID)
    did_dc = did_ud = did_hash = False
    in_ca = False
    for i, line in enumerate(lines):
        out.append(line)
        m = re.match(r'^(\s*)-\s*(\S*toon\.dc)\s*$', line)
        if m and not did_dc:
            out.append('%s- %s' % (m.group(1), platform_dc))
            did_dc = True
        if re.match(r'^uberdogs:\s*$', line) and not did_ud:
            # indentation of the first list item below
            ind = '    '
            for nxt in lines[i + 1:]:
                mm = re.match(r'^(\s*)-\s', nxt)
                if mm:
                    ind = mm.group(1)
                    break
            out += ['%s- class: TTPlatformManager' % ind, '%s  id: %d' % (ind, DO_ID),
                    '%s  anonymous: false' % ind, '']
            did_ud = True
        m = re.match(r'^(\s*)-\s*type:\s*clientagent\s*$', line)
        if m:
            in_ca = True
            out.append('%s  manual_dc_hash: %d' % (m.group(1), DC_HASH))
            did_hash = True
        elif in_ca and re.match(r'^\s*-\s*type:', line):
            in_ca = False
    if not (did_dc and did_ud and did_hash):
        raise ValueError('anchors missing: dc_files toon.dc=%s uberdogs=%s clientagent=%s' % (did_dc, did_ud, did_hash))
    if text.count('type: clientagent') != 1:
        sys.stderr.write('note: %d clientagent roles; each got manual_dc_hash\n' % text.count('type: clientagent'))
    return '\n'.join(out) + '\n'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst')
    ap.add_argument('--platform-dc', required=True)
    a = ap.parse_args()
    with open(a.src) as f:
        text = f.read()
    try:
        new = patch(text, a.platform_dc)
    except ValueError as e:
        sys.stderr.write('patch_astrond: %s\n' % e)
        return 2
    with open(a.dst, 'w') as f:
        f.write(new)
    return 0


if __name__ == '__main__':
    sys.exit(main())
