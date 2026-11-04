#!/usr/bin/env python3
"""Bake the Toon hood skies (Hood.skyFile) into RomFS .ttm models.

    .venv/bin/python -I tools/pipeline/bake_sky.py [--resources ref/resources] [--out romfs/baked/sky]

Upstream parents the sky to the camera with a CompassEffect (world rotation, world
Z 0), no depth test or write, in the background bin, with fog off (SkyUtil.startCloudSky,
Hood.load). TT_sky's two cloud groups turn at h and -0.8 h (cloudSkyTrack, h += 0.25
deg/s), so they bake to their own files and the runtime rotates them:

    tt.ttm, tt_cloud1.ttm, tt_cloud2.ttm   TT_sky (TTC, DG, OZ, Goofy Speedway, golf, tutorial)
    br.ttm                                 BR_sky (The Brrrgh, Donald's Dock)
    mm.ttm                                 MM_sky (Minnie's Melodyland)
    dl.ttm                                 DL_sky (Donald's Dreamland)
    cog.ttm                                cog_sky (Sellbot HQ courtyard and factory exterior,
                                           the only Cog places that startSky)

cog_sky keeps its dome and hills. CogHood.load puts MiddleGroup / InnerGroup (building
silhouettes) in the background bin without depth writes, so the opaque dome drawn after
them paints them over: upstream never shows them, and neither does this bake.

Cog HQ places that do not startSky have a sky box inside their own model instead, which
coghq_catalog stashes from the zone bake. Those bake here in world space (hq<zone>.ttm);
the runtime draws them first with no depth, scaled about the camera into the far plane.

The cloud cards are TransparencyAttrib.M_dual; drawn after the dome with no depth
they bake as plain alpha blending (soft cloud edges) instead of the usual alpha test.
"""
import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from panda3d.core import Filename, NodePath, TransparencyAttrib, loadPrcFileData  # noqa: E402

loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level warning\n'
                'textures-power-2 none')
from direct.showbase.ShowBase import ShowBase  # noqa: E402

import extract  # noqa: E402
import ttm  # noqa: E402

SKIES = {
    'tt': 'phase_3.5/models/props/TT_sky.bam',
    'br': 'phase_3.5/models/props/BR_sky.bam',
    'mm': 'phase_6/models/props/MM_sky.bam',
    'dl': 'phase_8/models/props/DL_sky.bam',
    'cog': 'phase_9/models/cogHQ/cog_sky.bam',
}
HIDDEN = ('**/MiddleGroup', '**/InnerGroup')
# zone -> (model, stashed sky node), as coghq_catalog's drop list.
HQ_SKIES = {
    10000: ('phase_12/models/bossbotHQ/CogGolfHub.bam', 'SkyBox'),
    10100: ('phase_12/models/bossbotHQ/CogGolfCourtyard.bam', 'SkyBox'),
    11100: ('phase_9/models/cogHQ/SellbotHQLobby.bam', 'skyGeo'),
    12000: ('phase_10/models/cogHQ/CashBotShippingStation.bam', 'SkyBox1'),
    12100: ('phase_10/models/cogHQ/VaultLobby.bam', 'SkyBox'),
    13000: ('phase_11/models/lawbotHQ/LawbotPlaza.bam', 'LB_Sky'),
    13100: ('phase_11/models/lawbotHQ/LB_CH_Lobby.bam', 'CH_SkyBox'),
}


def blend_clouds(node):
    for np_ in node.findAllMatches('**/+GeomNode'):
        gn = np_.node()
        for i in range(gn.getNumGeoms()):
            st = gn.getGeomState(i)
            if st.hasAttrib(TransparencyAttrib):
                gn.setGeomState(i, st.setAttrib(TransparencyAttrib.make(TransparencyAttrib.M_alpha)))


def bake(node, path):
    ex = extract.Extractor(max_tex=256)
    ex.add(node)
    return ttm.write(path, ex.textures, ex.batches())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--resources', default='ref/resources')
    ap.add_argument('--out', default='romfs/baked/sky')
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    base = ShowBase()
    report = {}
    for name, rel in SKIES.items():
        model = base.loader.loadModel(Filename.fromOsSpecific(os.path.abspath(os.path.join(args.resources, rel))))
        for pattern in HIDDEN:
            for hidden in model.findAllMatches(pattern):
                hidden.detachNode()
        clouds = [model.find(f'**/cloud{i}') for i in (1, 2)]
        for i, c in enumerate(clouds, 1):
            if c.isEmpty():
                continue
            c.detachNode()
            blend_clouds(c)
            report[f'{name}_cloud{i}'] = bake(c, os.path.join(args.out, f'{name}_cloud{i}.ttm'))
        report[name] = bake(model, os.path.join(args.out, f'{name}.ttm'))
    for zone, (rel, node) in sorted(HQ_SKIES.items()):
        model = base.loader.loadModel(Filename.fromOsSpecific(os.path.abspath(os.path.join(args.resources, rel))))
        root = NodePath('sky')
        found = model.findAllMatches(f'**/{node}')
        if not found.getNumPaths():
            sys.exit(f'{rel}: no {node}')
        for sky in found:
            sky.wrtReparentTo(root)
        report[f'hq{zone}'] = bake(root, os.path.join(args.out, f'hq{zone}.ttm'))
    print(json.dumps(report, indent=1, sort_keys=True))


if __name__ == '__main__':
    main()
