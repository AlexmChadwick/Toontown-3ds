"""Assembly of Toontown Central building interiors matching upstream PC logic.

Re-implements the deterministic layout, colour selection, and node hierarchy from:
- toontown/building/DistributedToonInterior.py
- toontown/building/DistributedHQInterior.py
- toontown/building/DistributedGagshopInterior.py
- toontown/building/DistributedPetshopInterior.py
- toontown/building/DistributedToonHallInterior.py
- toontown/building/ToonInteriorColors.py
- toontown/building/ToonInterior.py
- toontown/building/DistributedDoor.py
"""
import math
import random
from panda3d.core import ModelNode, NodePath, Point3, Vec4

from interior_dna import InteriorDNAStore

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
_doorBase = [
    Vec4(1.0, 1.0, 0.7, 1.0),
    Vec4(0.8, 0.5, 0.3, 1.0),
]
_floorBase = [
    Vec4(0.746, 1.0, 0.477, 1.0),
    Vec4(1.0, 0.684, 0.477, 1.0),
]

TTC_COLORS = {
    'TI_wainscotting': _wainscottingBase,
    'TI_wallpaper': _wallpaperBase,
    'TI_wallpaper_border': _wallpaperBorderBase,
    'TI_door': _doorBase,
    'TI_floor': _floorBase,
}

# TTC Playground interior definitions (zone 2000 safezone)
# Zone ID calculation: 2000 - 2000%100 + 500 + block = 2500 + block (DistributedBuildingMgrAI.py:199)
TTC_INTERIORS = {
    2513: {
        'block': 13,
        'name': 'Toon Hall',
        'interior_type': 'toonhall',
        'building_type': 'standard',
        'npcs': [
            {'id': 2018, 'name': 'Scientist 1', 'pos_index': 1},
            {'id': 2019, 'name': 'Scientist 2', 'pos_index': 2},
            {'id': 2020, 'name': 'Scientist 3', 'pos_index': 3},
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
            {'id': 2006, 'name': 'Clerk Clara', 'pos_index': 0},
            {'id': 2011, 'name': 'Clerk Cleo', 'pos_index': 1},
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
            {'id': 2013, 'name': 'Pet Clerk 1', 'pos_index': 0},
            {'id': 2014, 'name': 'Pet Clerk 2', 'pos_index': 1},
            {'id': 2015, 'name': 'Pet Clerk 3', 'pos_index': 2},
        ],
        'camera': (0.0, -16.0, 6.0, 0.0, 8.0, 5.0, 70),
    },
}

BLOCK_TO_ZONE = {spec['block']: zone for zone, spec in TTC_INTERIORS.items()}


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
    """Setup door parts matching DNADoor.setupDoor in upstream C++ libtoontown.

    - Flat frame stays visible, gets reparented to parent and tinted door_color
    - Holes (door_*_hole_left/right) are hidden
    - 3D swinging panels (door_*_left/right) are hidden and tinted door_color
    - Trigger collision polygon is renamed to trigger_name and attached to parent
    """
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


def assemble_interior(zone_id, block, loader, store, colors=TTC_COLORS):
    """Assemble an interior scene graph exactly as the PC client would.

    Returns (root_nodepath, spec_dict, selected_room_code).
    """
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

    elif itype == 'hq':
        # DistributedHQInterior.py:31-40, 148-170
        interior = loader.loadModel('phase_3.5/models/modules/HQ_interior').copyTo(root)
        interior.find('**/cream').hide()
        interior.find('**/crashed_piano').hide()
        interior.find('**/periscope').show()
        interior.find('**/speakers').show()

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

    return root, spec, selected_room


def extract_anchors(root):
    """Extract named anchors for .pts file.

    Includes door origins, door triggers, NPC origins, and utility origins.
    """
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
    """Compute spawn points and sidecar metadata for <zone>.json.

    - doorExitPoint: toon exiting door into interior (DistributedDoor.avatarExitTrack :449-455)
      Pos: (-1.5, -6.0, 0.025) relative to door_origin, heading = (door_h + 179) % 360
    - teleportPoint: teleport-in position (ToonInterior.enterTeleportIn :191-196)
      Pos: (0.0, 0.0, 0.025, 45.0) for pet shop, (2.5, 11.5, 0.025, 45.0) for others
    """
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
