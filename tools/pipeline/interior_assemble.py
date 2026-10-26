"""Assembly of Toontown Central, playground, and street building interiors matching upstream PC logic.

Re-implements the deterministic layout, colour selection, and node hierarchy from:
- toontown/building/DistributedToonInterior.py
- toontown/building/DistributedHQInterior.py
- toontown/building/DistributedGagshopInterior.py
- toontown/building/DistributedPetshopInterior.py
- toontown/building/DistributedKartShopInterior.py
- toontown/building/DistributedToonHallInterior.py
- toontown/building/ToonInteriorColors.py
- toontown/building/ToonInterior.py
- toontown/building/DistributedDoor.py
"""
import collections
import math
import os
import random
import re
from panda3d.core import ModelNode, NodePath, Point3, Vec4

from interior_dna import InteriorDNAStore

HERE = os.path.dirname(os.path.abspath(__file__))

# Color definitions from ToonInteriorColors.py:2-12, 23-27
_wainscottingBase = [
    Vec4(0.8, 0.5, 0.3, 1.0),
    Vec4(0.699, 0.586, 0.473, 1.0),
    Vec4(0.473, 0.699, 0.488, 1.0),
]
_wallpaperBase = [
    Vec4(1.0, 1.0, 0.7, 1.0),
    Vec4(0.8, 1.0, 0.7, 1.0),
    Vec4(0.4, 0.5, 0.4, 1.0),
    Vec4(0.5, 0.7, 0.6, 1.0),
]
_wallpaperBorderBase = [
    Vec4(1.0, 1.0, 0.7, 1.0),
    Vec4(0.8, 1.0, 0.7, 1.0),
    Vec4(0.4, 0.5, 0.4, 1.0),
    Vec4(0.5, 0.7, 0.6, 1.0),
]
_doorBaseTTC = [
    Vec4(1.0, 1.0, 0.7, 1.0),
    Vec4(0.8, 0.5, 0.3, 1.0),
]
_doorBaseSingle = [
    Vec4(1.0, 1.0, 0.7, 1.0),
]
_floorBase = [
    Vec4(0.746, 1.0, 0.477, 1.0),
    Vec4(1.0, 0.684, 0.477, 1.0),
]

TTC_COLORS = {
    'TI_wainscotting': _wainscottingBase,
    'TI_wallpaper': _wallpaperBase,
    'TI_wallpaper_border': _wallpaperBorderBase,
    'TI_door': _doorBaseTTC,
    'TI_floor': _floorBase,
}


def get_hood_colors(hood_id):
    """Retrieve color palette for a neighborhood per ToonInteriorColors.py:18-42."""
    if hood_id in (2000, 15000):  # ToontownCentral, Tutorial
        return TTC_COLORS
    return {
        'TI_wainscotting': _wainscottingBase,
        'TI_wallpaper': _wallpaperBase,
        'TI_wallpaper_border': _wallpaperBorderBase,
        'TI_door': _doorBaseSingle,
        'TI_floor': _floorBase,
    }


# TTC Playground interior definitions (zone 2000 safezone)
TTC_INTERIORS = {
    2513: {
        'block': 13,
        'name': 'Toon Hall',
        'interior_type': 'toonhall',
        'building_type': 'standard',
        'npcs': [
            {'id': 2018, 'name': 'Doctor Dimm', 'pos_index': 0},
            {'id': 2019, 'name': 'Doctor Surlee', 'pos_index': 1},
            {'id': 2020, 'name': 'Professor Prepostera', 'pos_index': 2},
            {'id': 2001, 'name': 'Flippy', 'pos_index': 3},
        ],
        'camera': (15.0, -15.0, 14.0, -15.0, 5.0, 6.0, 75),
    },
    2514: {
        'block': 14,
        'name': 'Toontown Bank',
        'interior_type': 'bank',
        'building_type': 'standard',
        'npcs': [
            {'id': 2002, 'name': 'Banker Bob', 'pos_index': 0},
        ],
        'camera': (0.0, -2.0, 5.0, 0.0, 14.0, 4.0, 70),
    },
    2516: {
        'block': 16,
        'name': 'Toontown School House',
        'interior_type': 'school',
        'building_type': 'standard',
        'npcs': [
            {'id': 2003, 'name': 'Professor Pete', 'pos_index': 0},
        ],
        'camera': (1.0, -6.0, 5.0, 8.0, 6.0, 4.0, 70),
    },
    2518: {
        'block': 18,
        'name': 'Toontown Library',
        'interior_type': 'library',
        'building_type': 'standard',
        'npcs': [
            {'id': 2005, 'name': 'Librarian Larry', 'pos_index': 0},
        ],
        'camera': (-5.0, -13.0, 5.0, 0.0, 2.0, 4.0, 70),
    },
    2519: {
        'block': 19,
        'name': 'Gag Shop',
        'interior_type': 'gagshop',
        'building_type': 'gagshop',
        'npcs': [
            {'id': 2006, 'name': 'Clerk Clark', 'pos_index': 0},
            {'id': 2011, 'name': 'Clerk Clara', 'pos_index': 1},
        ],
        'camera': (0.0, -2.0, 5.0, 0.0, 14.0, 4.0, 70),
    },
    2520: {
        'block': 20,
        'name': 'Toon HQ',
        'interior_type': 'hq',
        'building_type': 'hq',
        'npcs': [
            {'id': 2007, 'name': 'HQ Officer 1', 'pos_index': 0},
            {'id': 2008, 'name': 'HQ Officer 2', 'pos_index': 1},
            {'id': 2009, 'name': 'HQ Officer 3', 'pos_index': 2},
            {'id': 2010, 'name': 'HQ Officer 4', 'pos_index': 3},
        ],
        'camera': (-5.0, -8.0, 8.0, -5.0, 25.0, 3.0, 75),
    },
    2521: {
        'block': 21,
        'name': 'Clothing Shop',
        'interior_type': 'clotheshop',
        'building_type': 'standard',
        'npcs': [
            {'id': 2004, 'name': 'Tailor Tammy', 'pos_index': 0},
        ],
        'camera': (1.0, -6.0, 5.0, 8.0, 6.0, 4.0, 70),
    },
    2522: {
        'block': 22,
        'name': 'Pet Shop',
        'interior_type': 'petshop',
        'building_type': 'petshop',
        'npcs': [
            {'id': 2013, 'name': 'Clerk Barky', 'pos_index': 0},
            {'id': 2014, 'name': 'Clerk Purr', 'pos_index': 1},
            {'id': 2015, 'name': 'Clerk Bloop', 'pos_index': 2},
        ],
        'camera': (0.0, -16.0, 6.0, 0.0, 8.0, 5.0, 70),
    },
}

BLOCK_TO_ZONE = {spec['block']: zone for zone, spec in TTC_INTERIORS.items()}

# Registry of all DNA files containing landmark buildings
DNA_REGISTRY = [
    # Safezones (Playgrounds)
    (1000, 1000, True, 'phase_6/dna/donalds_dock_sz.dna'),
    (2000, 2000, True, 'phase_4/dna/toontown_central_sz.dna'),
    (3000, 3000, True, 'phase_8/dna/the_burrrgh_sz.dna'),
    (4000, 4000, True, 'phase_6/dna/minnies_melody_land_sz.dna'),
    (5000, 5000, True, 'phase_8/dna/daisys_garden_sz.dna'),
    (6000, 6000, True, 'phase_6/dna/outdoor_zone_sz.dna'),
    (8000, 8000, True, 'phase_6/dna/goofy_speedway_sz.dna'),
    (9000, 9000, True, 'phase_8/dna/donalds_dreamland_sz.dna'),
    # Streets
    (2000, 2100, False, 'phase_5/dna/toontown_central_2100.dna'),
    (2000, 2200, False, 'phase_5/dna/toontown_central_2200.dna'),
    (2000, 2300, False, 'phase_5/dna/toontown_central_2300.dna'),
    (1000, 1100, False, 'phase_6/dna/donalds_dock_1100.dna'),
    (1000, 1200, False, 'phase_6/dna/donalds_dock_1200.dna'),
    (1000, 1300, False, 'phase_6/dna/donalds_dock_1300.dna'),
    (3000, 3100, False, 'phase_8/dna/the_burrrgh_3100.dna'),
    (3000, 3200, False, 'phase_8/dna/the_burrrgh_3200.dna'),
    (3000, 3300, False, 'phase_8/dna/the_burrrgh_3300.dna'),
    (4000, 4100, False, 'phase_6/dna/minnies_melody_land_4100.dna'),
    (4000, 4200, False, 'phase_6/dna/minnies_melody_land_4200.dna'),
    (4000, 4300, False, 'phase_6/dna/minnies_melody_land_4300.dna'),
    (5000, 5100, False, 'phase_8/dna/daisys_garden_5100.dna'),
    (5000, 5200, False, 'phase_8/dna/daisys_garden_5200.dna'),
    (5000, 5300, False, 'phase_8/dna/daisys_garden_5300.dna'),
    (9000, 9100, False, 'phase_8/dna/donalds_dreamland_9100.dna'),
    (9000, 9200, False, 'phase_8/dna/donalds_dreamland_9200.dna'),
]

ALL_STORAGE_FILES = [
    'phase_4/dna/storage.dna',
    'phase_3.5/dna/storage_interior.dna',
    'phase_4/dna/storage_TT.dna',
    'phase_4/dna/storage_TT_sz.dna',
    'phase_6/dna/storage_DD.dna',
    'phase_6/dna/storage_DD_sz.dna',
    'phase_8/dna/storage_BR.dna',
    'phase_8/dna/storage_BR_sz.dna',
    'phase_6/dna/storage_MM.dna',
    'phase_6/dna/storage_MM_sz.dna',
    'phase_8/dna/storage_DG.dna',
    'phase_8/dna/storage_DG_sz.dna',
    'phase_8/dna/storage_DL.dna',
    'phase_8/dna/storage_DL_sz.dna',
    'phase_6/dna/storage_GS.dna',
    'phase_6/dna/storage_GS_sz.dna',
    'phase_5/dna/storage_town.dna',
    'phase_5/dna/storage_TT_town.dna',
    'phase_6/dna/storage_DD_town.dna',
    'phase_8/dna/storage_BR_town.dna',
    'phase_6/dna/storage_MM_town.dna',
    'phase_8/dna/storage_DG_town.dna',
    'phase_8/dna/storage_DL_town.dna',
]


# ---- tutorial (docs/design/tutorial.md section 4) ----
# The Toontorial's two interiors. Their DNA blocks come from tutorial_street.dna; the interior zone
# follows the usual branch + 500 + block (TutorialManagerAI allocates dynamic zones at run time;
# world/tutorial_zones.h maps them back to these).
TUTORIAL_DNA = 'phase_3.5/dna/tutorial_street.dna'
TUTORIAL_BRANCH = 20000
TUTORIAL_HOOD = 15000  # ToontownGlobals.Tutorial: ZoneUtil.getCanonicalHoodId under the tutorial override
# PlayGame.loadDnaStoreTutorial: the tutorial's DNA store holds only these.
TUTORIAL_STORAGE_FILES = ['phase_3.5/dna/storage_tutorial.dna', 'phase_3.5/dna/storage_interior.dna']
TUTORIAL_TOM_NPC_ID = 20000   # TutorialBuildingAI: NPCToons.createNPC(20000, ...), TTLocalizer.NPCToonNames
TUTORIAL_TOM_NAME = 'Tutorial Tom'
TUTORIAL_HQ_NPC_ID = 1000     # TutorialHQBuildingAI: NPCToons.createNPC(Quests.ToonHQ, ...)
TUTORIAL_HQ_NAME = 'HQ Harry'  # TTLocalizer.TutorialHQOfficerName


def is_tutorial_zone(zone_id):
    return TUTORIAL_BRANCH <= zone_id < TUTORIAL_BRANCH + 1000


def get_tutorial_interior_specs(resources):
    """Specs for Tutorial Tom's interior (block 2) and the tutorial Toon HQ (block 1)."""
    specs = {}
    for lm in parse_dna_landmarks(os.path.join(resources, TUTORIAL_DNA)):
        block = lm['block']
        zone = TUTORIAL_BRANCH + 500 + block
        if lm['building_type'] == 'hq':
            # TutorialHQBuildingAI: a DistributedHQInteriorAI with setTutorial(1).
            itype, title = 'hq', 'Toon HQ'
            npcs = [{'id': TUTORIAL_HQ_NPC_ID, 'name': TUTORIAL_HQ_NAME, 'pos_index': 0}]
            cam = (-5.0, -8.0, 8.0, -5.0, 25.0, 3.0, 75)
        else:
            # TutorialBuildingAI: DistributedTutorialInteriorAI (TutorialManagerAI's "gagBlock").
            itype, title = 'tutorial', TUTORIAL_TOM_NAME
            npcs = [{'id': TUTORIAL_TOM_NPC_ID, 'name': TUTORIAL_TOM_NAME, 'pos_index': 0}]
            cam = (0.0, -2.0, 5.0, 0.0, 14.0, 4.0, 70)
        specs[zone] = {
            'canonical_zone': zone,
            'hood_id': TUTORIAL_HOOD,
            'branch_id': TUTORIAL_BRANCH,
            'is_safezone': False,
            'dna_file': TUTORIAL_DNA,
            'block': block,
            'name': title,
            'building_type': lm['building_type'],
            'interior_type': itype,
            'tutorial': True,
            'resources': resources,  # the street view outside Tom's windows is assembled from DNA
            'npcs': npcs,
            'camera': cam,
        }
    return specs


def _add_tutorial_street_view(root, loader, resources):
    """DistributedTutorialInterior.setup: the tutorial street, seen through Tom's windows.

    The street is placed at (-17, 42, -0.5) facing 180, Tom's own landmark and the HQ's first
    door flat are stashed, and so is all of its collision. Its anchors are renamed so the
    interior's own door and NPC origins stay the only ones in the .pts.
    """
    import assemble
    import dna
    store = dna.Storage(loader, resources)
    for f in TUTORIAL_STORAGE_FILES:
        store.load(os.path.join(resources, f))
    tree = dna.parse_file(os.path.join(resources, TUTORIAL_DNA), [])
    street, _ = assemble.assemble(tree, store, seed=1)
    street.reparentTo(root)
    street.setPosHpr(-17, 42, -0.5, 180, 0, 0)
    street.find('**/tb2:toon_landmark_TT_A1_DNARoot').stash()
    flat = street.find('**/tb1:toon_landmark_hqTT_DNARoot/**/door_flat_0')
    if not flat.isEmpty():
        flat.stash()
    for c in street.findAllMatches('**/+CollisionNode'):
        c.stash()
    for pattern in ('**/door_origin*', '**/door_trigger*', '**/npc_*origin*', '**/sign_origin*',
                    '**/trophy_origin*'):
        for n in street.findAllMatches(pattern):
            n.setName('street_' + n.getName())
    return street
# ---- end tutorial ----


def parse_dna_landmarks(dna_path):
    """Parse landmark_building blocks from a DNA file."""
    with open(dna_path, 'r', encoding='utf-8') as f:
        text = f.read()
    results = []
    pos = 0
    while True:
        m = re.search(r'landmark_building\s+"([^"]+)"\s*\[', text[pos:])
        if not m:
            break
        root_name = m.group(1)
        start_idx = pos + m.end()
        depth = 1
        i = start_idx
        while i < len(text) and depth > 0:
            if text[i] == '[':
                depth += 1
            elif text[i] == ']':
                depth -= 1
            i += 1
        body = text[start_idx:i - 1]
        pos = i

        bm = re.match(r'(?:sz|tb)(\d+):', root_name)
        block = int(bm.group(1)) if bm else None
        btype_m = re.search(r'building_type\s*\[\s*"([^"]+)"\s*\]', body)
        code_m = re.search(r'code\s*\[\s*"([^"]+)"\s*\]', body)
        title_m = re.search(r'title\s*\[\s*"([^"]+)"\s*\]', body)
        article_m = re.search(r'article\s*\[\s*"([^"]+)"\s*\]', body)

        results.append({
            'root_name': root_name,
            'block': block,
            'building_type': btype_m.group(1) if btype_m else 'toon',
            'code': code_m.group(1) if code_m else '',
            'title': title_m.group(1) if title_m else '',
            'article': article_m.group(1) if article_m else '',
        })
    return results


_DNA_CACHE = {}


class _SignStorage:
    """The DNA storage as sign_text sees it, with private copies of the dynamic (.ttf) fonts.

    A DynamicTextFont packs glyphs into its pages in the order they are first rendered, so with
    the shared FontPool font a sign's UVs would depend on every sign baked before it in the
    process. A fresh copy per sign keeps each interior's bake independent of bake order.
    """

    def __init__(self, storage):
        self._storage = storage
        self._sign_fonts = {}
        for code, path in storage.fonts.items():
            if path.lower().endswith('.ttf'):
                font = storage.loader.loadFont(path, okMissing=True)
                if font is not None:
                    self._sign_fonts[code] = font.makeCopy()

    def __getattr__(self, name):
        return getattr(self._storage, name)


class _SignAssembler:
    """The few assembler fields sign_text.assemble_baseline uses."""

    def __init__(self, storage):
        self.storage = _SignStorage(storage)
        self.sign_letters = 0
        self.sign_baselines = 0
        self.unresolved = collections.Counter()

    def _find(self, code):
        return self.storage.find_node(code)


def get_landmark_sign(zone_id, block, resources, loader, store):
    """Assemble the landmark sign of tb<block> on the street (or playground) outside zone_id.

    Upstream (DistributedToonInterior.setup) copies '**/tb<block>:*_landmark_*_DNARoot/**/sign'
    from the exterior the toon just left. Block numbers repeat on every street, so the exterior
    is the interior zone's own branch: interior = cell - cell % 100 + 500 + block, hence
    branch = zone - zone % 100 - 500 (2601 -> 2100, 2518 -> 2000).
    """
    import dna
    import sign_text

    branch = zone_id - zone_id % 100 - 500
    for hood_id, branch_id, is_sz, rel_path in DNA_REGISTRY:
        if branch_id != branch:
            continue
        p = os.path.join(resources, rel_path)
        if not os.path.exists(p):
            continue
        if p not in _DNA_CACHE:
            try:
                _DNA_CACHE[p] = dna.parse_file(p)
            except Exception:
                continue
        tree = _DNA_CACHE[p]

        sign_node = None

        def find_sign(node):
            nonlocal sign_node
            if node.kind == 'landmark_building':
                m = re.match(r'(?:sz|tb)(\d+):', node.name or '')
                if m and int(m.group(1)) == block:
                    sign_node = node.find('sign')
                    return
            for c in node.children:
                if sign_node is not None:
                    return
                find_sign(c)

        find_sign(tree)
        if sign_node is not None:
            asm = _SignAssembler(store.storage)
            code = sign_node.get('code')
            src = store.findNode(code) if code else None
            root = NodePath('sign')
            if src is not None:
                src.copyTo(root)
            for b in sign_node.find_all('baseline'):
                sign_text.assemble_baseline(asm, b, root)
            return root
    return None


def _load_npc_data(resources):
    """Load NPCToons dictionary from ref/open-toontown."""
    try:
        import sys
        import builtins
        from unittest.mock import MagicMock
        sys.modules.setdefault('panda3d.otp', MagicMock())
        if not hasattr(builtins, 'base'):
            builtins.base = MagicMock()
        open_tt_dir = os.path.abspath(os.path.join(resources, '..', 'open-toontown'))
        if open_tt_dir not in sys.path:
            sys.path.insert(0, open_tt_dir)
        from toontown.toon import NPCToons
        NPCToons.generateZone2NpcDict()
        return NPCToons
    except Exception as e:
        print(f"Note: Could not import NPCToons ({e}); proceeding with fallback names.")
        return None


def get_all_interior_specs(resources):
    """Extract complete interior specs for all 536 buildings across all hoods and streets."""
    npc_toons = _load_npc_data(resources)
    specs = {}

    for hood_id, branch_id, is_sz, rel_path in DNA_REGISTRY:
        p = os.path.join(resources, rel_path)
        if not os.path.exists(p):
            continue
        landmarks = parse_dna_landmarks(p)
        for lm in landmarks:
            block = lm['block']
            canonical_zone = branch_id + 500 + block
            btype = lm['building_type']
            title = lm['title']
            if not title:
                if btype == 'gagshop':
                    title = 'Gag Shop'
                elif btype == 'hq':
                    title = 'Toon HQ' if is_sz else f'Toon HQ ({block})'
                elif btype == 'clotheshop':
                    title = 'Clothing Shop'
                elif btype == 'petshop':
                    title = 'Pet Shop'
                elif btype == 'kartshop':
                    title = 'Goofy Speedway Kart Shop'
                else:
                    title = f'Building {block}'

            itype = btype
            if itype in ('toon', 'standard', 'clotheshop', 'bank', 'school', 'library'):
                if canonical_zone == 2513:
                    itype = 'toonhall'
                elif canonical_zone == 2514:
                    itype = 'bank'
                elif canonical_zone == 2516:
                    itype = 'school'
                elif canonical_zone == 2518:
                    itype = 'library'
                elif btype == 'clotheshop':
                    itype = 'clotheshop'
                else:
                    itype = 'standard'

            # NPCs from upstream NPCToons
            npcs = []
            if npc_toons and hasattr(npc_toons, 'zone2NpcDict'):
                npc_ids = npc_toons.zone2NpcDict.get(canonical_zone, [])
                for i, nid in enumerate(npc_ids):
                    name = str(npc_toons.getNPCName(nid))
                    npcs.append({'id': nid, 'name': name, 'pos_index': i})
            elif canonical_zone in TTC_INTERIORS:
                npcs = TTC_INTERIORS[canonical_zone].get('npcs', [])

            # Camera
            if itype == 'toonhall':
                cam = (15.0, -15.0, 14.0, -15.0, 5.0, 6.0, 75)
            elif itype == 'hq':
                cam = (-5.0, -8.0, 8.0, -5.0, 25.0, 3.0, 75)
            elif itype == 'petshop':
                cam = (0.0, -16.0, 6.0, 0.0, 8.0, 5.0, 70)
            elif itype == 'kartshop':
                cam = (0.0, -25.0, 10.0, 0.0, 10.0, 4.0, 70)
            elif itype in ('school', 'clotheshop'):
                cam = (1.0, -6.0, 5.0, 8.0, 6.0, 4.0, 70)
            elif itype == 'library':
                cam = (-5.0, -13.0, 5.0, 0.0, 2.0, 4.0, 70)
            else:
                cam = (0.0, -2.0, 5.0, 0.0, 14.0, 4.0, 70)

            specs[canonical_zone] = {
                'canonical_zone': canonical_zone,
                'hood_id': hood_id,
                'branch_id': branch_id,
                'is_safezone': is_sz,
                'dna_file': rel_path,
                'block': block,
                'name': title,
                'building_type': btype,
                'interior_type': itype,
                'npcs': npcs,
                'camera': cam,
            }

    return specs


def random_dna_item(category, find_func, rng, store):
    """Upstream DistributedToonInterior.py:48-51."""
    count = store.getNumCatalogCodes(category)
    if count == 0:
        return None
    idx = rng.randint(0, count - 1)
    code = store.getCatalogCode(category, idx)
    return find_func(code)


def replace_random_in_model(model, zone_id, rng, store, colors):
    """Upstream DistributedToonInterior.py:53-80, DistributedGagshopInterior.py:30-55."""
    base_tag = 'random_'
    npc = model.findAllMatches('**/' + base_tag + '???_*')
    b = len(base_tag)
    for i in range(npc.getNumPaths()):
        np_ = npc.getPath(i)
        name = np_.getName()
        category = name[b + 4:]
        key1 = name[b]
        key2 = name[b + 1]

        if key1 == 'm':
            m = random_dna_item(category, store.findNode, rng, store)
            if m is not None:
                new_np = m.copyTo(np_)
                for c in new_np.findAllMatches('**/collision'):
                    c.stash()
                if key2 == 'r':
                    replace_random_in_model(new_np, zone_id, rng, store, colors)
        elif key1 == 't':
            tex = random_dna_item(category, store.findTexture, rng, store)
            if tex is not None:
                np_.setTexture(tex, 100)
            new_np = np_

        if key2 == 'c' and category in colors:
            # Deterministic wallpaper color sync: DistributedToonInterior.py:76
            if category in ('TI_wallpaper', 'TI_wallpaper_border'):
                rng.seed(zone_id)
                new_np.setColorScale(rng.choice(colors[category]))
            else:
                new_np.setColorScale(rng.choice(colors[category]))


def _setup_door_parts(door_np, parent, color, trigger_name):
    """Setup door parts matching DNADoor.setupDoor in upstream C++ libtoontown."""
    door_frame = door_np.find('door_*_flat')
    if not door_frame.isEmpty():
        door_frame.wrtReparentTo(parent)
        door_frame.setColor(color)

    hole_l = door_np.find('door_*_hole_left')
    hole_r = door_np.find('door_*_hole_right')
    if not hole_l.isEmpty():
        hole_l.hide()
    if not hole_r.isEmpty():
        hole_r.hide()

    left = door_np.find('door_*_left')
    right = door_np.find('door_*_right')
    if not left.isEmpty():
        left.setColor(color)
        left.hide()
    if not right.isEmpty():
        right.setColor(color)
        right.hide()

    trigger = door_np.find('door_*_trigger')
    if not trigger.isEmpty():
        trigger.setName(trigger_name)
        trigger.wrtReparentTo(parent)


def assemble_interior(zone_id, block, loader, store, spec=None, colors=None):
    """Assemble an interior scene graph exactly as the PC client would.

    Returns (root_nodepath, spec_dict, selected_room_code).
    """
    if spec is None:
        spec = TTC_INTERIORS.get(zone_id)
        if spec is None:
            spec = {
                'block': block,
                'name': f'Building {block}',
                'interior_type': 'standard',
                'building_type': 'standard',
                'npcs': [],
                'camera': (0.0, -2.0, 5.0, 0.0, 14.0, 4.0, 70),
            }

    itype = spec['interior_type']
    root = NodePath(f'interior_{zone_id}')

    if colors is None:
        hood_id = zone_id - zone_id % 1000
        # ---- tutorial ----
        if is_tutorial_zone(zone_id):
            hood_id = TUTORIAL_HOOD  # ToonInteriorColors.colors[Tutorial]
        # ---- end tutorial ----
        colors = get_hood_colors(hood_id)

    # Deterministic RNG seeded by zone ID:
    # DistributedToonInterior.py:84, DistributedHQInterior.py:150, etc.
    rng = random.Random()
    rng.seed(zone_id)

    # Door model name selection: DistributedHQInterior.py:139-144, DistributedToonInterior.py:90-95
    door_model = store.findNode('door_double_round_ur')
    selected_room = itype

    if itype == 'gagshop':
        # DistributedGagshopInterior.py:73-88
        interior = loader.loadModel('phase_4/models/modules/gagShop_interior').copyTo(root)
        replace_random_in_model(interior, zone_id, rng, store, colors)

        door_origin = interior.find('**/door_origin;+s')
        door_np = door_model.copyTo(door_origin)
        door_origin.setScale(0.8, 0.8, 0.8)
        door_origin.setPos(door_origin, 0, -0.025, 0)
        door_color = rng.choice(colors['TI_door'])
        _setup_door_parts(door_np, interior, door_color, f'door_trigger_{block}')

    # ---- tutorial ----
    elif itype == 'tutorial':
        # DistributedTutorialInterior.setup: the tutorial room with the street outside its windows.
        interior = loader.loadModel('phase_3.5/models/modules/toon_interior_tutorial').copyTo(root)
        _add_tutorial_street_view(root, loader, spec['resources'])
        replace_random_in_model(interior, zone_id, rng, store, colors)
        # door_double_round_ul with its last letter swapped: door_double_round_ur.
        door_origin = interior.find('**/door_origin;+s')
        door_np = door_model.copyTo(door_origin)
        door_origin.setScale(0.8, 0.8, 0.8)
        door_origin.setPos(door_origin, 0, -0.025, 0)
        door_color = rng.choice(colors['TI_door'])
        _setup_door_parts(door_np, interior, door_color, f'door_trigger_{block}')
    # ---- end tutorial ----
    elif itype == 'hq':
        # DistributedHQInterior.py:31-40, 148-170
        interior = loader.loadModel('phase_3.5/models/modules/HQ_interior').copyTo(root)
        interior.find('**/cream').hide()
        interior.find('**/crashed_piano').hide()
        interior.find('**/periscope').show()
        interior.find('**/speakers').show()
        # ---- tutorial ----
        if spec.get('tutorial'):
            # TutorialHQBuildingAI: interior.setTutorial(1) -> DistributedHQInterior.setTutorial
            # hides the periscope and the speakers.
            interior.find('**/periscope').hide()
            interior.find('**/speakers').hide()
        # ---- end tutorial ----

        for origin in interior.findAllMatches('**/door_origin*'):
            idx_str = origin.getName()[len('door_origin_'):]
            new_node = interior.attachNewNode(ModelNode(f'door_{idx_str}'))
            door_np = door_model.copyTo(new_node)
            origin.setScale(0.8, 0.8, 0.8)
            origin.setPos(origin, 0, -0.025, 0)
            door_np.setPosHprScale(origin, (0, 0, 0), (0, 0, 0), (1, 1, 1))
            door_color = rng.choice(colors['TI_door'])
            _setup_door_parts(door_np, new_node, door_color, f'door_trigger_{block}_{idx_str}')

    elif itype == 'petshop':
        # DistributedPetshopInterior.py:74-95
        interior = loader.loadModel('phase_4/models/modules/PetShopInterior').copyTo(root)
        fish = loader.loadModel('phase_4/models/props/interiorfish-zero')
        if fish is not None:
            fish.reparentTo(interior)
            fish.setColorScale(0.8, 0.9, 1, 0.8)
            fish.setScale(0.8)
            fish.setPos(0, 6, -4)

        replace_random_in_model(interior, zone_id, rng, store, colors)

        door_origin = interior.find('**/door_origin;+s')
        door_np = door_model.copyTo(door_origin)
        door_origin.setScale(0.8, 0.8, 0.8)
        # Pet shop uses -0.25 offset: DistributedPetshopInterior.py:90
        door_origin.setPos(door_origin, 0, -0.25, 0)
        door_color = rng.choice(colors['TI_door'])
        _setup_door_parts(door_np, interior, door_color, f'door_trigger_{block}')

    elif itype == 'kartshop':
        # DistributedKartShopInterior.py:34-37
        interior = loader.loadModel('phase_6/models/karting/KartShop_Interior').copyTo(root)
        # KartShop_Interior already contains door_trigger_1, door_trigger_2, door_origin_1, door_origin_2

    elif itype == 'toonhall':
        # DistributedToonHallInterior.py:67-87, 178-205, 572-577
        room_node = store.findNode('TI_hall')
        interior = room_node.copyTo(root)

        ropes = loader.loadModel('phase_4/models/modules/tt_m_ara_int_ropes')
        if ropes is not None:
            ropes.reparentTo(interior)

        flat_sm = loader.loadModel('phase_3.5/models/modules/tt_m_ara_int_sillyMeterFlat')
        if flat_sm is not None:
            flat_sm.reparentTo(interior)

        replace_random_in_model(interior, zone_id, rng, store, colors)

        door_origin = interior.find('**/door_origin;+s')
        door_np = door_model.copyTo(door_origin)
        door_origin.setScale(0.8, 0.8, 0.8)
        door_origin.setPos(door_origin, 0, -0.025, 0)
        door_color = rng.choice(colors['TI_door'])
        _setup_door_parts(door_np, interior, door_color, f'door_trigger_{block}')

    else:
        # Standard toon interior: DistributedToonInterior.py:83-104
        room_code = random_dna_item('TI_room', lambda c: c, rng, store)
        selected_room = room_code
        room_node = store.findNode(room_code)
        interior = room_node.copyTo(root)

        replace_random_in_model(interior, zone_id, rng, store, colors)

        door_origin = interior.find('**/door_origin;+s')
        door_np = door_model.copyTo(door_origin)
        door_origin.setScale(0.8, 0.8, 0.8)
        door_origin.setPos(door_origin, 0, -0.025, 0)
        door_color = rng.choice(colors['TI_door'])
        _setup_door_parts(door_np, interior, door_color, f'door_trigger_{block}')

    # Landmark interior sign: DistributedToonInterior.py:105-126
    sign_origin = interior.find('**/sign_origin;+s')
    if sign_origin.isEmpty():
        sign_origin = interior.find('**/sign_origin')
    if not sign_origin.isEmpty():
        res_path = getattr(store, 'resources', None)
        if not res_path:
            res_path = os.path.join(HERE, '..', '..', 'ref', 'ref', 'resources')
            if not os.path.exists(res_path):
                res_path = os.path.join(HERE, '..', '..', 'ref', 'resources')
        sign_np = get_landmark_sign(zone_id, block, res_path, loader, store)
        if sign_np is not None:
            new_sign = sign_np.copyTo(sign_origin)
            new_sign.setDepthWrite(1, 1)
            ll, ur = Point3(), Point3()
            new_sign.calcTightBounds(ll, ur)
            width = ur[0] - ll[0]
            height = ur[2] - ll[2]
            SIGN_LEFT = -4.0
            SIGN_RIGHT = 4.0
            SIGN_BOTTOM = -3.5
            SIGN_TOP = 1.5
            if width > 1e-4 and height > 1e-4:
                x_scale = (SIGN_RIGHT - SIGN_LEFT) / width
                z_scale = (SIGN_TOP - SIGN_BOTTOM) / height
                scale = min(x_scale, z_scale)
                x_center = (ur[0] + ll[0]) / 2.0
                z_center = (ur[2] + ll[2]) / 2.0
                new_sign.setPosHprScale(
                    (SIGN_RIGHT + SIGN_LEFT) / 2.0 - x_center * scale,
                    -0.1,
                    (SIGN_TOP + SIGN_BOTTOM) / 2.0 - z_center * scale,
                    0.0, 0.0, 0.0,
                    scale, scale, scale,
                )

    return root, spec, selected_room


def extract_anchors(root):
    """Extract named anchors for .pts file."""
    patterns = [
        '**/door_origin*',
        '**/door_trigger*',
        '**/npc_*origin*',
        '**/sign_origin*',
        '**/trophy_origin*',
    ]
    points = []
    seen = set()
    for pat in patterns:
        for np_ in root.findAllMatches(pat):
            name = np_.getName()
            if name in seen:
                continue
            seen.add(name)
            p = np_.getPos(root)
            h = np_.getH(root)
            points.append((name, float(p[0]), float(p[1]), float(p[2]), float(h)))
    return points


def compute_spawn_data(root, zone_id, block, spec):
    """Compute spawn points and sidecar metadata for <zone>.json."""
    door_origin = root.find('**/door_origin*')
    door_exit = {'x': 0.0, 'y': 0.0, 'z': 0.025, 'h': 180.0}
    doors = []

    door_nodes = root.findAllMatches('**/door_origin*')
    for d_node in door_nodes:
        name = d_node.getName()
        dp = d_node.getPos(root)
        dh = d_node.getH(root)
        # Point relative to door_origin: x = -1.5, y = -6.0, z = 0.025
        exit_pos = root.getRelativePoint(d_node, Point3(-1.5, -6.0, 0.025))
        exit_h = (dh + 179.0) % 360.0
        doors.append({
            'name': name,
            'origin': {'x': round(dp[0], 3), 'y': round(dp[1], 3), 'z': round(dp[2], 3), 'h': round(dh, 2)},
            'exitPoint': {'x': round(exit_pos[0], 3), 'y': round(exit_pos[1], 3), 'z': round(exit_pos[2], 3), 'h': round(exit_h, 2)},
        })

    if doors:
        door_exit = doors[0]['exitPoint']

    is_petshop = (spec['interior_type'] == 'petshop')
    teleport = {
        'x': 0.0 if is_petshop else 2.5,
        'y': 0.0 if is_petshop else 11.5,
        'z': 0.025,
        'h': 45.0,
    }

    npc_origins = []
    for npc_node in root.findAllMatches('**/npc_*origin*'):
        p = npc_node.getPos(root)
        h = npc_node.getH(root)
        npc_origins.append({
            'name': npc_node.getName(),
            'x': round(p[0], 3),
            'y': round(p[1], 3),
            'z': round(p[2], 3),
            'h': round(h, 2),
        })

    return {
        'zoneId': zone_id,
        'block': block,
        'interiorType': spec['interior_type'],
        'buildingType': spec['building_type'],
        'name': spec['name'],
        'spawnPoint': door_exit,
        'doorExitPoint': door_exit,
        'teleportPoint': teleport,
        'doors': doors,
        'npcOrigins': npc_origins,
        'npcs': spec.get('npcs', []),
    }
