#!/usr/bin/env python3
"""Bake Cog attack props and actor animations to ETC1 inside AGENTS.md budgets."""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import argparse
from battle_common import DATA, capture, ttm, write_json, bake_actor
from direct.showbase.ShowBase import ShowBase
from panda3d.core import NodePath, TextNode, loadPrcFileData

loadPrcFileData('', 'window-type offscreen\nload-display p3tinydisplay\nframebuffer-alpha true')

PROPS = (
    # (phase, name, model, [clip])
    (5, 'baseball', 'baseball'),
    (5, 'bird', 'bird'),
    (5, 'bounced-check', 'bounced-check'),
    (5, 'calculator', 'calculator-mod', 'calculator-chan'),
    (5, 'can', 'can'),
    (3.5, 'clip-on-tie', 'clip-on-tie-mod', 'clip-on-tie-chan'),
    (5, 'dagger', 'dagger'),
    (5, 'evil-eye', 'evil-eye'),
    (5, 'golf-ball', 'golf-ball'),
    (5, 'golf-club', 'golf-club'),
    (5, 'half-windsor', 'half-windsor'),
    (5, 'newspaper', 'newspaper'),
    (5, 'pad', 'pad'),
    (5, 'pen', 'pen'),
    (5, 'pencil', 'pencil'),
    (5, 'pink-slip', 'pink-slip'),
    (5, 'power-tie', 'power-tie'),
    (5, 'redtape', 'redtape'),
    (5, 'redtape-tube', 'redtape-tube'),
    (5, 'rollodex', 'roll-o-dex'),
    (5, 'rubber-stamp', 'rubber-stamp'),
    (5, 'sharpener', 'sharpener'),
    (3.5, 'shredder', 'shredder'),
    (3.5, 'shredder-paper', 'shredder-paper-mod', 'shredder-paper-chan'),
    (5, 'smile', 'smile-mod', 'smile-chan'),
    (4, 'stormcloud', 'stormcloud-mod', 'stormcloud-chan'),
    (5, 'teeth', 'teeth-mod', 'teeth-chan'),
    (5, 'watercooler', 'watercooler'),
)


def bake_static(out, name, root, manifest, source):
    ex, batches = capture(root, True, animated=False)
    filename = name + '.ttm'
    stats = ttm.write(out / filename, ex.textures, batches)
    root.removeNode()
    manifest['props'][name] = dict(file=filename, source_model=source, clips={}, **stats)
    print(f'Static {name:16s}: {stats["file_bytes"]} bytes')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out', type=Path, default=DATA / 'props')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    base = ShowBase()
    manifest = {'version': 1, 'props': {}}

    for row in PROPS:
        phase = row[0]
        name = row[1]
        model = f'phase_{phase:g}/models/props/{row[2]}'
        if len(row) == 4:
            clip = f'phase_{phase:g}/models/props/{row[3]}'
            info = bake_actor(base, args.out, name, model, {'action': clip}, {'full': 4000})['full']
            print(f'Actor  {name:16s}: {info["file_bytes"]} bytes')
        else:
            prop = base.loader.loadModel(model)
            root = NodePath('prop_root')
            prop.reparentTo(root)
            ex, batches = capture(root, True, animated=False)
            filename = name + '.ttm'
            stats = ttm.write(args.out / filename, ex.textures, batches)
            root.removeNode()
            info = dict(file=filename, source_model=model, clips={}, **stats)
            print(f'Static {name:16s}: {stats["file_bytes"]} bytes')
        manifest['props'][name] = info

    # Props MovieSuitAttacks builds at runtime instead of loading a model file.
    # WriteOff: BattleParticles.getParticle('checkmark'), a card in suit-particles.
    particles = base.loader.loadModel('phase_3.5/models/props/suit-particles')
    root = NodePath('prop_root')
    particles.find('**/checkmark').copyTo(root)
    bake_static(args.out, 'checkmark', root, manifest, 'phase_3.5/models/props/suit-particles:checkmark')
    # Canned / Fired: __makeCancelledNodePath, TTLocalizer.MovieSuitCancelled in the suit font
    # (TTLocalizer.SuitFont, pixelsPerUnit 40, spaceAdvance 0.25, lineHeight 1.0), red, two-sided.
    font = base.loader.loadFont('phase_3/models/fonts/vtRemingtonPortable.ttf', pixelsPerUnit=40,
                                spaceAdvance=0.25, lineHeight=1.0)
    text = TextNode('CANCELLED')
    text.setFont(font)
    text.setText('CANCELLED\nCANCELLED\nCANCELLED')
    text.setAlign(TextNode.ACenter)
    root = NodePath('CancelledTop')
    front = root.attachNewNode(text.generate())
    front.setColor(0.7, 0, 0, 1)
    back = root.attachNewNode(text.generate())
    back.setColor(0.7, 0, 0, 1)
    back.setH(180)
    root.flattenStrong()
    bake_static(args.out, 'CancelledTop', root, manifest, 'MovieSuitAttacks.__makeCancelledNodePath')

    write_json(args.out / 'suit_props_manifest.json', manifest)
    base.destroy()
    print('Bake suit props finished successfully.')


if __name__ == '__main__':
    main()
