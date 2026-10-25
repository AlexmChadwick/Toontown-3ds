#!/usr/bin/env python3
"""Bake collision (floor / wall / camera / triggers) from a model or an assembled DNA zone
into a .ttc file (format spec: tools/pipeline/collide.py).

    # single model (any .bam/.egg; a scene saved with NodePath.writeBamFile also works)
    .venv/bin/python -I tools/pipeline/bake_collision.py \
        ref/resources/phase_4/models/neighborhoods/toontown_central.bam \
        romfs/baked/toontown_central.ttc

    # assembled DNA zone: input is the zone .dna, storage files in load order
    .venv/bin/python -I tools/pipeline/bake_collision.py \
        ref/resources/phase_4/dna/toontown_central_sz.dna romfs/baked/ttc_sz.ttc \
        --storage ref/resources/phase_4/dna/storage.dna \
                  ref/resources/phase_4/dna/storage_TT.dna \
                  ref/resources/phase_4/dna/storage_TT_sz.dna

Prints a stats JSON (sizes, class counts, grid occupancy, trigger names). Fails if the
file exceeds --max-kb (default 1024 KB). Deterministic output.
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
DEFAULT_RESOURCES = os.path.join(HERE, '..', '..', 'ref', 'resources')
if not os.path.isdir(DEFAULT_RESOURCES):
    DEFAULT_RESOURCES = os.path.join(HERE, '..', '..', 'ref', 'ref', 'resources')


def load_scene(args, base):
    from panda3d.core import Filename
    if args.input.lower().endswith('.dna'):
        import assemble
        import dna
        store = dna.Storage(base.loader, args.resources)
        for path in args.storage:
            store.load(path)
        zone = dna.parse_file(args.input, [])
        root, _ = assemble.assemble(zone, store, seed=args.seed,
                                    name=os.path.splitext(os.path.basename(args.input))[0])
        return root
    model = base.loader.loadModel(Filename.fromOsSpecific(os.path.abspath(args.input)))
    if args.node:
        model = model.find(args.node)
        if model.isEmpty():
            sys.exit(f'node {args.node} not found')
    return model


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('input', help='.bam/.egg model, or a DNA zone .dna (assembled with --storage)')
    ap.add_argument('out')
    ap.add_argument('--storage', nargs='+', default=[], help='storage .dna files in load order')
    ap.add_argument('--resources', default=DEFAULT_RESOURCES)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--node', help='only this sub-node (find pattern)')
    ap.add_argument('--cell', type=float, default=8.0, help='grid cell size (units)')
    ap.add_argument('--fallback-floor', choices=['auto', 'on', 'off'], default='auto')
    ap.add_argument('--max-kb', type=int, default=1024)
    args = ap.parse_args()
    if args.input.lower().endswith('.dna') and not args.storage:
        ap.error('a .dna input needs --storage files')

    from panda3d.core import loadPrcFileData
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                    'textures-power-2 none')
    from direct.showbase.ShowBase import ShowBase
    import collide

    base = ShowBase()
    scene = load_scene(args, base)
    fb = {'auto': 'auto', 'on': True, 'off': False}[args.fallback_floor]
    mesh = collide.extract(scene, fallback_floor=fb)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or '.', exist_ok=True)
    stats = mesh.write(args.out, cell_size=args.cell, max_bytes=args.max_kb * 1024)
    print(json.dumps(stats, indent=1))


if __name__ == '__main__':
    main()
