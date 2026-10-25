"""Independent Panda DC streams and seeded maze choices; Python -I -B."""

from pathlib import Path
import struct
import sys

sys.dont_write_bytecode = True
from panda3d.direct import DCFile
from direct.showbase.RandomNumGen import RandomNumGen

ROOT = Path(__file__).resolve().parents[2]
# ref/ in the main checkout; agent worktrees link it as ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').exists() else ROOT / 'ref/ref'
OUT = ROOT / "sdmc-out/cogdo-test"
OUT.mkdir(parents=True, exist_ok=True)
dc = DCFile()
for name in ("otp.dc", "toon.dc"):
    assert dc.read(str(REF / "open-toontown/etc" / name))
assert dc.getHash() == 547155168
names = [
    "DistributedCogdoInterior",
    "DistributedCogdoBarrel",
    "DistCogdoMazeGame",
    "DistCogdoFlyingGame",
    "DistCogdoBoardroomGame",
    "DistCogdoCraneGame",
    "DistCogdoCrane",
    "DistCogdoCraneMoneyBag",
    "DistCogdoCraneCog",
]
records = []
enters = []
for offset, name in enumerate(names):
    cls = dc.getClassByName(name)
    required = []
    optional = []
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is not None or not (
            field.isRequired() or field.isRam()
        ):
            continue
        value = bytes(field.getDefaultValue())
        records.append(
            struct.pack("<HHI", cls.getNumber(), field.getNumber(), len(value)) + value
        )
        if field.isRequired():
            required.append(value)
        else:
            optional.append(struct.pack("<H", field.getNumber()) + value)
    data = b"".join(required) + struct.pack("<H", len(optional)) + b"".join(optional)
    enters.append(struct.pack("<IHI", 1000 + offset, cls.getNumber(), len(data)) + data)
for name, field_name, value in [
    ("DistributedCogdoInterior", "setToons", "([42,43,0,0],0)"),
    ("DistributedCogdoInterior", "setSuits", "([50,51],[52,53],[100,50])"),
    ("DistributedCogdoInterior", "setState", '("WaitForAllToonsInside",-32760)'),
    ("DistCogdoGame", "setDifficultyOverrides", "(2147483647,-1)"),
    ("DistCogdoGame", "setDifficultyOverrides", "(32768,9000)"),
    ("DistCogdoMazeGame", "setNumSuits", "([4,5,5])"),
    ("DistCogdoMazeGame", "doAction", "(2,0,-32760)"),
    ("DistCogdoMazeGame", "toonUsedGag", "(42,1.5,-2,90,-32760)"),
    (
        "DistCogdoCrane",
        "setCablePos",
        "(255,24.5,315,[(1,2,-6),(2,3,-13),(3,4,-20)],-32760)",
    ),
]:
    cls = dc.getClassByName(name)
    field = cls.getFieldByName(field_name)
    data = bytes(field.parseString(value))
    assert data
    records.append(
        struct.pack("<HHI", cls.getNumber(), field.getNumber(), len(data)) + data
    )
(OUT / "vectors.bin").write_bytes(b"".join(records))
(OUT / "enters.bin").write_bytes(b"".join(enters))
seeds = []
for seed in [0, 1, 42, 1000, 400000001, 4294967295]:
    parent = RandomNumGen(seed)
    rng = RandomNumGen(parent)
    barriers = list(range(12))
    rng.shuffle(barriers)
    quadrants = list(range(9))
    rng.shuffle(quadrants)
    angles = [rng.randint(0, 3) for _ in range(9)]
    seeds.append(struct.pack("<I12B9B9B", seed, *barriers, *quadrants, *angles))
(OUT / "maze_seeds.bin").write_bytes(b"".join(seeds))
print(
    "Panda Cogdo fields:",
    len(records),
    "child enters:",
    len(enters),
    "maze seeds:",
    len(seeds),
)
# Execute only the original pure layout/spawn methods, independently of C++.
import ast
import runpy
from types import SimpleNamespace

upstream = REF / "open-toontown/toontown"
data = SimpleNamespace(**runpy.run_path(str(upstream / "cogdominium/CogdoMazeData.py")))
barriers = (
    [(7, 34), (8, 34), (9, 34), (10, 34)],
    [(24, 34), (25, 34), (26, 34), (27, 34)],
    [(41, 34), (42, 34), (43, 34), (44, 34)],
    [(7, 17), (8, 17), (9, 17), (10, 17)],
    [(24, 17), (25, 17), (26, 17), (27, 17)],
    [(41, 17), (42, 17), (43, 17), (44, 17)],
    [(17, 41), (17, 42), (17, 43), (17, 44)],
    [(17, 24), (17, 25), (17, 26), (17, 27)],
    [(17, 7), (17, 8), (17, 9), (17, 10)],
    [(34, 41), (34, 42), (34, 43), (34, 44)],
    [(34, 24), (34, 25), (34, 26), (34, 27)],
    [(34, 7), (34, 8), (34, 9), (34, 10)],
)
globals_ref = SimpleNamespace(
    FrameWallThickness=1, TotalBarriers=12, NumBarriers=3, MazeBarriers=barriers
)
namespace = {
    "RandomNumGen": RandomNumGen,
    "Globals": globals_ref,
    "CogdoMazeData": data,
}
source = ast.parse((upstream / "cogdominium/CogdoMaze.py").read_text())
cls = next(
    node
    for node in source.body
    if isinstance(node, ast.ClassDef) and node.name == "CogdoMazeFactory"
)
methods = [
    node
    for node in cls.body
    if isinstance(node, ast.FunctionDef)
    and node.name
    in ("__init__", "getMazeData", "_gatherQuadrantData", "_generateMazeData")
]
exec(
    compile(
        ast.Module(body=methods, type_ignores=[]), "pinned CogdoMazeFactory", "exec"
    ),
    namespace,
)
factory_type = type(
    "ReferenceFactory", (), {node.name: namespace[node.name] for node in methods}
)
source = ast.parse((upstream / "minigame/MazeBase.py").read_text())
cls = next(
    node
    for node in source.body
    if isinstance(node, ast.ClassDef) and node.name == "MazeBase"
)
methods = [
    node
    for node in cls.body
    if isinstance(node, ast.FunctionDef)
    and node.name in ("isWalkable", "createRandomSpotsList")
]
exec(
    compile(ast.Module(body=methods, type_ignores=[]), "pinned MazeBase", "exec"),
    namespace,
)
maze_type = type(
    "ReferenceMaze", (), {node.name: namespace[node.name] for node in methods}
)
records = []
for seed in [0, 1, 42, 1000, 400000001, 4294967295]:
    parent = RandomNumGen(seed)
    factory = factory_type(parent, 3, 3)
    maze_data = factory.getMazeData()
    maze = maze_type()
    maze.width = maze_data["width"]
    maze.height = maze_data["height"]
    maze.collisionTable = maze_data["collisionTable"]
    spawns = maze.createRandomSpotsList([4, 5, 5], parent)
    records.append(
        struct.pack("<I", seed)
        + bytes(value for row in maze.collisionTable for value in row)
        + bytes(value for group in spawns for point in group for value in point)
    )
(OUT / "maze_maps.bin").write_bytes(b"".join(records))
print("Pinned Python maze cells and suit spawns:", len(records), "seeds")
# Use the actual upstream direction chooser, plus its tick ordering and boss-drop
# RNG draws. This catches differences that layout-only golden vectors cannot.
source = ast.parse((upstream / "minigame/MazeSuit.py").read_text())
original = next(
    node
    for node in source.body
    if isinstance(node, ast.ClassDef) and node.name == "MazeSuit"
)
kept = [
    node
    for node in original.body
    if isinstance(node, ast.Assign)
    and any(
        isinstance(target, ast.Name)
        and (target.id.startswith("DIR_") or target.id == "oppositeDirections")
        for target in node.targets
    )
]
kept += [
    node
    for node in original.body
    if isinstance(node, ast.FunctionDef)
    and node.name in ("__applyDirection", "__chooseNewWalkDirection")
]
minimal = ast.ClassDef(
    name="MazeSuit", bases=[], keywords=[], body=kept, decorator_list=[]
)
module = ast.fix_missing_locations(ast.Module(body=[minimal], type_ignores=[]))
walk_env = {}
exec(compile(module, "pinned MazeSuit chooser", "exec"), walk_env)
Suit = walk_env["MazeSuit"]
traces = []
for seed in [0, 1, 42, 1000, 400000001, 4294967295]:
    parent = RandomNumGen(seed)
    factory = factory_type(parent, 3, 3)
    layout = factory.getMazeData()
    model = maze_type()
    model.width = model.height = 52
    model.collisionTable = layout["collisionTable"]
    spawns = model.createRandomSpotsList([4, 5, 5], parent)
    suits = []
    for kind, group in enumerate(spawns):
        for x, y in group:
            suit = Suit()
            suit.TX = suit.nextX = x
            suit.TY = suit.nextY = y
            suit.direction = 1
            suit.rng = RandomNumGen(seed + len(suits) * 10)
            suit.difficulty = 0.5
            suit._walkSameDirectionProb = 1
            suit._walkTurnAroundProb = 100
            suit.nextTick = len(suits) * 256 // 20
            suit.period = [192, 64, 160][kind]
            suit.occupiedTiles = [(x, y)]
            suit.dropTimer = 1
            suit.kind = kind
            suit.maze = model
            suits.append(suit)
    for frame in range(600):
        tick = frame * 256 // 30
        updates = sorted(
            (when, index)
            for index, suit in enumerate(suits)
            for when in range(suit.nextTick, tick + 1, suit.period)
        )
        current = 0
        for offset, (when, index) in enumerate(updates):
            if when > current:
                current = when
                for later, other in updates[offset + 1 :]:
                    if later > when:
                        break
                    suits[other].occupiedTiles = [
                        (suits[other].nextX, suits[other].nextY)
                    ]
            suit = suits[index]
            blocked = [
                point
                for other in suits
                if other is not suit
                for point in other.occupiedTiles
            ]
            suit.TX, suit.TY = suit.nextX, suit.nextY
            suit.direction = suit._MazeSuit__chooseNewWalkDirection(blocked)
            suit.nextX, suit.nextY = suit._MazeSuit__applyDirection(
                suit.direction, suit.TX, suit.TY
            )
            suit.occupiedTiles = [(suit.TX, suit.TY), (suit.nextX, suit.nextY)]
            suit.nextTick += suit.period
            if suit.kind == 0:
                suit.dropTimer += 1
                if suit.dropTimer >= 3:
                    suit.dropTimer = 0
                    spots = [
                        (x, y)
                        for x in range(suit.TX - 5, suit.TX + 5)
                        for y in range(suit.TY - 5, suit.TY + 5)
                        if model.isWalkable(x, y)
                    ]
                    if spots:
                        suit.rng.choice(spots)
        traces.append(
            struct.pack("<I", tick)
            + bytes(
                value
                for suit in suits
                for value in (suit.TX, suit.TY, suit.nextX, suit.nextY, suit.direction)
            )
        )
(OUT / "maze_walks.bin").write_bytes(b"".join(traces))
print("Pinned Python Cogdo suit chooser/tick/drop traces: 3600 frames")
# Panda transform/sort oracle for anti-cheat-sensitive cooler numbering.
from panda3d.core import NodePath

cooler_bytes = (
    ROOT / "sdmc-out/3ds/toontown3ds/data/cogdo/maze_coolers.bin"
).read_bytes()
count = struct.unpack_from("<H", cooler_bytes, 6)[0]
coolers = [
    struct.unpack_from("<I4f", cooler_bytes, 8 + 20 * index) for index in range(count)
]
records = []
for seed in [0, 1, 42, 1000, 400000001, 4294967295]:
    parent = RandomNumGen(seed)
    rng = RandomNumGen(parent)
    barriers = list(range(12))
    rng.shuffle(barriers)
    quadrants = list(range(9))
    rng.shuffle(quadrants)
    angles = [rng.randint(0, 3) * 90 for _ in quadrants]
    world = NodePath("world")
    poses = []
    for index, quadrant in enumerate(quadrants):
        node = world.attachNewNode("quad")
        node.setPos((index % 3 - 1) * 51, (index // 3 - 1) * 51, 0)
        node.setH(angles[index])
        for kind, x, y, z, h in coolers:
            if kind != quadrant:
                continue
            cooler = node.attachNewNode("cooler")
            cooler.setPos(x, y, z)
            cooler.setH(h)
            position = cooler.getPos(world)
            heading = cooler.getH(world)
            trigger = cooler.attachNewNode("trigger")
            trigger.setPos(0, -1.5, 0)
            poses.append((tuple(position), heading, tuple(trigger.getPos(world))))
    poses.sort()
    records.append(
        struct.pack("<IH", seed, len(poses))
        + b"".join(struct.pack("<7f", *pos, h, *trigger) for pos, h, trigger in poses)
    )
    world.removeNode()
(OUT / "maze_coolers.bin").write_bytes(b"".join(records))
print("Panda cooler transforms and network numbering: 6 seeds")

# Exercise dispatch through the real Client for the inherited exterior/interior
# elevators and battle class, rather than just the dedicated Cogdo parser.
streams = []
for index, name in enumerate(
    (
        "DistributedCogdoElevatorExt",
        "DistributedCogdoElevatorInt",
        "DistributedCogdoBattleBldg",
        "DistributedBuilding",
    )
):
    cls = dc.getClassByName(name)
    required = []
    optional = []
    for field_index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(field_index)
        if field.asMolecularField() is not None:
            continue
        if field.isRequired() and field.isBroadcast():
            required.append(bytes(field.getDefaultValue()))
        elif field.isRam():
            optional.append(
                struct.pack("<H", field.getNumber()) + bytes(field.getDefaultValue())
            )
    body = (
        struct.pack("<IIIH", 2000 + index, 100, 61000, cls.getNumber())
        + b"".join(required)
        + struct.pack("<H", len(optional))
        + b"".join(optional)
    )
    streams.append(struct.pack("<I", len(body)) + body)
(OUT / "families.bin").write_bytes(b"".join(streams))
