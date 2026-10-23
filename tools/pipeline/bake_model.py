#!/usr/bin/env python3
"""Bake one static .bam model into a .ttm file.

    .venv/bin/python -I tools/pipeline/bake_model.py ref/resources/phase_4/models/props/xxx.bam out.ttm
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from panda3d.core import Filename, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                'textures-power-2 none')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import extract  # noqa: E402
import ttm  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bam')
    ap.add_argument('out')
    ap.add_argument('--max-tex', type=int, default=128)
    ap.add_argument('--node', help='only bake this sub-node (find pattern)')
    args = ap.parse_args()

    base = ShowBase()
    model = base.loader.loadModel(Filename.fromOsSpecific(os.path.abspath(args.bam)))
    if args.node:
        model = model.find(args.node)
        if model.isEmpty():
            sys.exit(f'node {args.node} not found')
    ex = extract.Extractor(max_tex=args.max_tex)
    ex.add(model)
    stats = ttm.write(args.out, ex.textures, ex.batches())
    print(json.dumps(stats))


if __name__ == '__main__':
    main()
