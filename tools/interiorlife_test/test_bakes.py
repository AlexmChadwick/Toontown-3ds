#!/usr/bin/env python3
"""Host test: the interior life bakes are deterministic.

Runs the real baker (bake_interior_props.bake_all) twice into fresh SD trees and checks that
both runs, and the committed sdmc-out copy when present, are byte-identical.
"""
import hashlib
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'pipeline'))

from direct.showbase.ShowBase import ShowBase  # noqa: E402

import bake_interior_props  # noqa: E402


def sha256_file(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    base = ShowBase()
    res = bake_interior_props.find_resources()
    runs = [tempfile.mkdtemp(prefix='interior_life_bake_') for _ in range(2)]
    try:
        outputs = [bake_interior_props.bake_all(base, res, sd) for sd in runs]
        failed = False
        for first, second in zip(*outputs):
            rel = os.path.relpath(first, runs[0])
            h1 = sha256_file(first)
            h2 = sha256_file(second)
            committed = os.path.join(ROOT, 'sdmc-out', rel)
            h3 = sha256_file(committed) if os.path.exists(committed) else h1
            if h1 != h2 or h1 != h3:
                print(f'FAIL: {rel} differs ({h1[:12]} / {h2[:12]} / sdmc-out {h3[:12]})')
                failed = True
            else:
                print(f'PASS: {rel} ({os.path.getsize(first)} bytes, {h1[:16]})')
        if failed:
            sys.exit(1)
        print('ALL BAKE DETERMINISM TESTS PASSED.')
    finally:
        for sd in runs:
            shutil.rmtree(sd, ignore_errors=True)


if __name__ == '__main__':
    main()
