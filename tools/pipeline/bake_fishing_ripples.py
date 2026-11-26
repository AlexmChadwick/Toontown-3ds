#!/usr/bin/env python3
"""Bake the fishing bob's splash ripples (toontown/effects/Ripples: BattleProps 'ripples').

DistributedFishingSpot.__showBob plays Ripples at the bob, on the water (waterLevel + 0.025),
under the angle node at scale 0.4: phase_4/models/props/ripples is a 24-card SequenceNode
(10 x 10 units, flat, frame rate 24) whose cards sample three different palette pages, played
once at rate 1 and then hidden. Its first child is raised 0.1 and the effect draws in the
'fixed' bin.

Each card is rendered straight down into one 48 px cell of a 256 x 256 RGBA4 page, and each
frame becomes one batch (a 10 x 10 quad at z 0.1) so the runtime shows one frame at a time
(Renderer::setOnlyBatch), like the battle explosion flipbook.

    .venv/bin/python -I tools/pipeline/bake_fishing_ripples.py --resources <ref/resources>
        [--out sdmc-out/3ds/toontown3ds/data/fishing] [--preview ripples.png]
"""
import argparse
import os
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

CELL, PAGE, SS = 48, 256, 4


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--resources', default=os.path.join(ROOT, 'ref', 'resources'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'sdmc-out', '3ds', 'toontown3ds', 'data', 'fishing'))
    ap.add_argument('--preview')
    args = ap.parse_args()

    from panda3d.core import loadPrcFileData
    loadPrcFileData('', f'window-type offscreen\nwin-size {CELL * SS} {CELL * SS}\naudio-library-name null\n'
                    'notify-level warning\ntextures-power-2 none\nframebuffer-alpha 1\nmodel-cache-dir\n'
                    f'model-path {args.resources}\ndefault-model-extension .bam')
    import numpy as np
    from direct.showbase.ShowBase import ShowBase
    from panda3d.core import NodePath, OrthographicLens, PNMImage, TransparencyAttrib
    import ttm

    base = ShowBase()
    base.setBackgroundColor(0, 0, 0, 0)
    lens = OrthographicLens()
    lens.setFilmSize(10, 10)
    lens.setNearFar(-100, 100)
    base.cam.node().setLens(lens)
    base.camera.setPosHpr(0, 0, 50, 0, -90, 0)  # straight down, +Y up the image

    model = base.loader.loadModel('phase_4/models/props/ripples')
    seq = model.find('**/+SequenceNode')
    frames = list(seq.getChildren())
    cols = PAGE // CELL
    if len(frames) > cols * cols:
        raise SystemExit('too many ripple frames for the page')
    sheet = PNMImage(PAGE, PAGE, 4)
    sheet.fill(0, 0, 0)
    sheet.alphaFill(0)
    batches = []
    for i, frame in enumerate(frames):
        holder = NodePath('frame')
        holder.reparentTo(base.render)
        card = frame.copyTo(holder)
        card.setMat(frame.getMat(model))
        card.setZ(0)
        holder.setTransparency(TransparencyAttrib.MAlpha)
        holder.setTwoSided(True)
        base.graphicsEngine.renderFrame()
        base.graphicsEngine.renderFrame()
        shot = PNMImage()
        base.win.getScreenshot().store(shot)
        cell = PNMImage(CELL, CELL, 4)
        cell.gaussianFilterFrom(1.0, shot)
        cx, cy = (i % cols) * CELL, (i // cols) * CELL
        sheet.copySubImage(cell, cx, cy)
        holder.removeNode()
        # UVs: v = 1 at the page's top row (TTM rows are stored top first).
        u0, u1 = (cx + 0.5) / PAGE, (cx + CELL - 0.5) / PAGE
        v1, v0 = 1 - (cy + 0.5) / PAGE, 1 - (cy + CELL - 0.5) / PAGE
        quad = np.array([[-5, -5, 0.1, u0, v0, 1, 1, 1, 1], [5, -5, 0.1, u1, v0, 1, 1, 1, 1],
                         [5, 5, 0.1, u1, v1, 1, 1, 1, 1], [-5, 5, 0.1, u0, v1, 1, 1, 1, 1]], dtype=np.float64)
        batches.append(ttm.Batch(0, ttm.BLEND_ALPHA, True, 0, 0, quad, np.array([0, 1, 2, 0, 2, 3], dtype=np.int64)))

    px = np.empty((PAGE, PAGE, 4), dtype=np.uint8)
    for y in range(PAGE):
        for x in range(PAGE):
            c = sheet.getXelA(x, y)
            px[y, x] = [round(c[0] * 255), round(c[1] * 255), round(c[2] * 255), round(c[3] * 255)]
    a = px[..., 3:4].astype(np.float32) / 255.0  # straight alpha: undo the clear background's black
    px[..., :3] = np.round(np.where(a > 0, np.clip(px[..., :3] / np.maximum(a, 1e-6), 0, 255), 0)).astype(np.uint8)
    if args.preview:
        sheet.write(args.preview)
    texture = ttm.Texture('ripples', px, ttm.GPU_RGBA4, ttm.GPU_CLAMP_TO_EDGE, ttm.GPU_CLAMP_TO_EDGE, mipmaps=False)
    os.makedirs(args.out, exist_ok=True)
    out = os.path.join(args.out, 'ripples.ttm')
    stats = ttm.write(out, [texture], batches)
    print(f'{out}: {len(batches)} frames at {seq.node().getFrameRate()} fps, {stats["file_bytes"]} bytes')
    base.destroy()


if __name__ == '__main__':
    main()
