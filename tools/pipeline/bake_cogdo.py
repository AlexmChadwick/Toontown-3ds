#!/usr/bin/env python3
"""Deterministic native Cogdo kit. Run with Python -I -B; ref is read-only."""

import ast
import hashlib
import json
import os
from pathlib import Path
import runpy
import struct
import sys

sys.dont_write_bytecode = True
os.environ["OPENBLAS_NUM_THREADS"] = "1"
os.environ["OMP_NUM_THREADS"] = "1"
os.environ["TT_ETC1_THREADS"] = "3"
ROOT = Path(__file__).resolve().parents[2]
# ref/ in the main checkout; agent worktrees link it as ref/ref.
REF = ROOT / 'ref' if (ROOT / 'ref/open-toontown/toontown').exists() else ROOT / 'ref/ref'
sys.path.insert(0, str(ROOT / "tools/pipeline"))
import extract
import collide
import ttm
from panda3d.core import BamCache, NodePath, loadPrcFileData

BamCache.getGlobalPtr().setActive(False)
loadPrcFileData("", "window-type none\naudio-library-name null\nmodel-cache-dir\n")
from direct.showbase.ShowBase import ShowBase

RES = REF / "resources"
UPSTREAM = REF / "open-toontown"
OUT = ROOT / "sdmc-out/3ds/toontown3ds/data/cogdo"


def main():
    base = ShowBase()
    OUT.mkdir(parents=True, exist_ok=True)
    # NPCToons.createLocalNPC uses these DNA tuples, or newToonRandom(seed, gender, 1).
    sys.path.insert(0, str(UPSTREAM))
    from toontown.toon import ToonDNA

    npc_source = ast.parse((UPSTREAM / "toontown/toon/NPCToons.py").read_text())
    dictionary = next(
        node.value
        for node in npc_source.body
        if isinstance(node, ast.Assign)
        and any(
            isinstance(target, ast.Name) and target.id == "NPCToonDict"
            for target in node.targets
        )
    )
    descriptions = {
        ast.literal_eval(key): value
        for key, value in zip(dictionary.keys, dictionary.values)
    }
    for node in ast.walk(npc_source):
        if isinstance(node, ast.Assign) and isinstance(node.value, ast.Tuple):
            for target in node.targets:
                if (
                    isinstance(target, ast.Subscript)
                    and isinstance(target.value, ast.Name)
                    and target.value.id == "NPCToonDict"
                ):
                    descriptions[ast.literal_eval(target.slice)] = node.value
    npc_ids = sorted(descriptions)
    records = []
    for npc_id in npc_ids:
        description = descriptions[npc_id]
        properties = ast.literal_eval(description.elts[2])
        dna = ToonDNA.ToonDNA()
        if properties == "r":
            dna.newToonRandom(npc_id, ast.literal_eval(description.elts[3]), 1)
        else:
            dna.newToonFromProperties(*properties)
        net_string = dna.makeNetString()
        if isinstance(net_string, str):
            net_string = net_string.encode("latin1")
        if len(net_string) != 15:
            raise ValueError(f"NPC {npc_id}: unexpected DNA size")
        records.append(struct.pack("<I", npc_id) + net_string)
    (OUT / "shop_owners.bin").write_bytes(
        struct.pack("<4sHH", b"BND1", 1, len(records)) + b"".join(records)
    )
    report = {
        "version": 1,
        "simplifier": "fast-simplification==0.2.0 (single-thread quadrics)",
        "models": {},
        "collisions": {},
        "metadata": {},
    }

    def load(path):
        model = base.loader.loadModel(str(RES / (path + ".bam")))
        if model.isEmpty():
            raise ValueError(path)
        return model

    def bake(name, model, collision=False):
        extractor = extract.Extractor(max_tex=128)
        extractor.add(model)
        batches = extractor.batches()
        # Shared BAM arrays often include vertices unused by individual geoms.
        import numpy as np

        for batch in batches:
            used = np.unique(batch.indices)
            batch.verts = batch.verts[used]
            batch.indices = np.searchsorted(used, batch.indices)
        if name == "barrel_room":
            # The shipped BAM includes disconnected construction geometry hundreds
            # of units below/outside its playable corridor. Retain every triangle
            # intersecting the room, including its floor and enclosing walls.
            for batch in batches:
                triangles = batch.indices.reshape(-1, 3)
                positions = batch.verts[triangles, :3]
                low, high = positions.min(axis=1), positions.max(axis=1)
                keep = (
                    (high[:, 0] >= -25)
                    & (low[:, 0] <= 25)
                    & (high[:, 1] >= -85)
                    & (low[:, 1] <= 20)
                    & (high[:, 2] >= -5)
                    & (low[:, 2] <= 60)
                )
                batch.indices = triangles[keep].reshape(-1)
            batches = [batch for batch in batches if len(batch.indices)]
        original_triangles = sum(len(batch.indices) // 3 for batch in batches)
        target = (
            800
            if name.startswith("maze_quadrant_")
            else 4000
            if name == "barrel_room"
            else 128
            if name == "barrel"
            else 5000
            if name == "suite"
            else original_triangles
        )
        # Quadrics preserve planar cabinet/wall surfaces far better than spatial
        # clustering. Keep UV/color seams separate, and retain each surviving
        # source vertex's attributes. No random sampling or parallel optimizer.
        if original_triangles > target:
            sys.path.append(str(ROOT / "sdmc-out/cogdo-tools"))
            import fast_simplification

            if fast_simplification.__version__ != "0.2.0":
                raise ValueError(
                    "Install pinned stock wheel fast-simplification==0.2.0"
                )
            for batch in batches:
                allowance = max(
                    32, int(target * (len(batch.indices) // 3) / original_triangles)
                )
                if len(batch.indices) // 3 <= allowance:
                    continue
                # Never collapse large architectural faces: doing so can erase
                # an entire disconnected floor or wall at a low global target.
                vertices = batch.verts
                original_faces = batch.indices.reshape(-1, 3)
                corners = vertices[original_faces, :3]
                areas = (
                    np.linalg.norm(
                        np.cross(
                            corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]
                        ),
                        axis=1,
                    )
                    / 2
                )
                threshold = 2 if name.startswith("maze_quadrant_") else 5
                protected = original_faces[areas >= threshold]
                detail = original_faces[areas < threshold]
                allowance = max(16, allowance - len(protected))
                positions, inverse = np.unique(
                    np.round(vertices[:, :3], 4), axis=0, return_inverse=True
                )
                triangles = inverse[detail].astype(np.int32)
                valid = (
                    (triangles[:, 0] != triangles[:, 1])
                    & (triangles[:, 1] != triangles[:, 2])
                    & (triangles[:, 2] != triangles[:, 0])
                )
                triangles = triangles[valid]
                _, unique = np.unique(
                    np.sort(triangles, axis=1), axis=0, return_index=True
                )
                triangles = triangles[np.sort(unique)]
                if len(triangles):
                    points, faces = fast_simplification.simplify(
                        positions.astype(np.float64),
                        triangles,
                        target_count=min(allowance, len(triangles)),
                        agg=12,
                    )
                    if len(faces) > allowance:
                        # Some disconnected micro-details cannot collapse. Keep
                        # the largest remaining detail faces in stable source order.
                        corners = points[faces]
                        sizes = np.linalg.norm(
                            np.cross(
                                corners[:, 1] - corners[:, 0],
                                corners[:, 2] - corners[:, 0],
                            ),
                            axis=1,
                        )
                        selected = np.argsort(-sizes, kind="stable")[:allowance]
                        faces = faces[np.sort(selected)]
                    nearest = [
                        np.argmin(np.sum((vertices[:, :3] - point) ** 2, axis=1))
                        for point in points
                    ]
                    attributes = vertices[nearest].copy()
                    attributes[:, :3] = points
                else:
                    faces = np.empty((0, 3), dtype=np.int32)
                    attributes = vertices[:0]
                combined = np.concatenate(
                    (protected.reshape(-1), faces.reshape(-1) + len(vertices))
                )
                combined_vertices = np.concatenate((vertices, attributes))
                used = np.unique(combined)
                batch.verts = combined_vertices[used]
                batch.indices = np.searchsorted(used, combined)
            batches = [batch for batch in batches if len(batch.indices)]
        stats = ttm.write(str(OUT / (name + ".ttm")), extractor.textures, batches)
        if (
            stats["geometry_bytes"] > 6 * 1024 * 1024
            or stats["texture_bytes"] > 8 * 1024 * 1024
            or stats["triangles"] > 15000
            or stats["batches"] > 150
        ):
            raise ValueError(name + ": model budget exceeded " + repr(stats))
        stats["source_triangles"] = original_triangles
        report["models"][name] = stats
        if collision:
            if name == "maze_frame":
                # MazeBase's player is constrained by the cell table on the z=0
                # plane. The frame BAM contains only elevated trim, not a floor.
                mesh = collide.CollisionMesh()
                mesh.add_tri((-78, -78, 0), (78, -78, 0), (78, 78, 0), collide.FLOOR)
                mesh.add_tri((-78, -78, 0), (78, 78, 0), (-78, 78, 0), collide.FLOOR)
            else:
                mesh = collide.extract(model, fallback_floor="auto")
            report["collisions"][name] = mesh.write(
                str(OUT / (name + ".ttc")), max_bytes=1024 * 1024
            )
        model.removeNode()

    prefix = "phase_5/models/cogdominium/"
    shell_root = NodePath("shell")
    shell = load(prefix + "tt_m_ara_cbe_fieldOfficeMoverShaker")
    shell.reparentTo(shell_root)
    shell.setX(15.463)  # DistributedBuilding.setupCogdo, relative to the DNA anchor.
    shell.setColorScale(0.6, 0.6, 0.6, 1)
    origin = shell.find("**/*_door_origin")
    if origin.isEmpty():
        raise ValueError("Cogdo shell door origin missing")
    elevator = load(prefix + "tt_m_ara_csa_elevatorB")
    elevator.reparentTo(origin)
    report["metadata"]["exterior_door"] = list(origin.getPos(shell_root))
    bake("shell", shell_root)
    sources = {
        "elevator": prefix + "tt_m_ara_csa_elevatorB",
        "maze_frame": prefix + "tt_m_ara_cmg_level",
        "maze_divider": prefix + "tt_m_ara_cmg_grouping_blockerDivider",
        "exit_door": prefix + "tt_m_ara_csa_exitDoor",
        "balloon": prefix + "tt_m_ara_cmg_waterBalloon",
        "memo": prefix + "tt_m_ara_csa_memo",
        "cabinet": prefix + "tt_m_ara_cmg_cabinetSmFalling",
        "barrel_room": prefix + "tt_m_ara_cbr_barrelRoom",
        "barrel": prefix + "tt_m_ara_cbr_laughBarrel",
        "battle_room": "phase_7/models/modules/boss_suit_office",
        "prototype_room": "phase_10/models/cogHQ/EndVault",
        "crane_arm": "phase_10/models/cogHQ/CBCraneArm",
        "crane_magnet": "phase_10/models/cogHQ/CBMagnet",
        "crane_controls": "phase_10/models/cogHQ/CBCraneControls",
        "crane_stick": "phase_10/models/cogHQ/CBCraneStick",
        "money_bag": "phase_10/models/cashbotHQ/MoneyBag",
    }
    for index in range(1, 10):
        sources[f"maze_quadrant_{index}"] = prefix + f"tt_m_ara_cmg_quadrant{index}"
    for name, path in sources.items():
        model = load(path)
        if name == "maze_frame":
            for key in ("elevator_loc", "exit_loc"):
                node = model.find("**/" + key)
                report["metadata"][key] = list(node.getPos(model))
        if name.startswith("maze_quadrant_"):
            coolers = []
            for node in model.findAllMatches("**/*waterCooler"):
                coolers.append([*node.getPos(model), *node.getHpr(model)])
            report["metadata"][name + "_coolers"] = sorted(coolers)
        bake(
            name,
            model,
            name in ("barrel_room", "battle_room", "prototype_room", "maze_frame"),
        )
    suite = load(prefix + "tt_m_ara_crg_penthouse")
    cage = suite.find("**/cage")
    door = suite.find("**/cage_door")
    report["metadata"]["cage"] = list(cage.getPos(suite))
    report["metadata"]["cage_door"] = list(door.getPos(suite))
    for name, node in [("suite_cage", cage), ("suite_door", door)]:
        holder = NodePath(name)
        node.wrtReparentTo(holder)
        bake(name, holder)
    bake("suite", suite, True)

    report["flying"] = (
        "Unsupported forced prototype: pinned resources contain no flying level models."
    )

    maze_data = runpy.run_path(str(UPSTREAM / "toontown/cogdominium/CogdoMazeData.py"))
    tables = maze_data["QuadrantCollisions"]
    blob = bytearray(struct.pack("<4sHH", b"CGM1", 1, 9))
    for table in tables.values():
        for angle in (0, 90, 180, 270):
            blob.extend(value for row in table[angle] for value in row)
    (OUT / "maze_cells.bin").write_bytes(blob)
    # CGP1 has only finite f32 values: exterior door, maze entrance/exit, cage and cage-door hinge.
    values = [
        v
        for name in ("exterior_door", "elevator_loc", "exit_loc", "cage", "cage_door")
        for v in report["metadata"][name]
    ]
    (OUT / "points.bin").write_bytes(struct.pack("<4sHH15f", b"CGP1", 1, 5, *values))
    (OUT / "maze_coolers.json").write_text(
        json.dumps(
            {
                name: value
                for name, value in report["metadata"].items()
                if name.endswith("_coolers")
            },
            sort_keys=True,
        )
        + "\n"
    )
    # CGW1 stores each cooler in its quadrant-local coordinates (plus heading).
    coolers = []
    for index in range(1, 10):
        for pose in report["metadata"][f"maze_quadrant_{index}_coolers"]:
            coolers.append((index - 1, *pose[:3], pose[3]))
    (OUT / "maze_coolers.bin").write_bytes(
        struct.pack("<4sHH", b"CGW1", 1, len(coolers))
        + b"".join(struct.pack("<I4f", *record) for record in coolers)
    )
    groups = {
        "maze": ["maze_frame"]
        + [f"maze_quadrant_{index}" for index in range(1, 10)]
        + ["maze_divider", "exit_door", "balloon", "memo", "cabinet"],
        "suite": ["suite", "suite_cage", "suite_door", "elevator"],
        "barrels": ["barrel_room", "barrel", "elevator"],
        "crane": [
            "prototype_room",
            "crane_arm",
            "crane_magnet",
            "crane_controls",
            "crane_stick",
            "money_bag",
        ],
        "boardroom": ["prototype_room", "elevator"],
        "battle_floor": ["battle_room", "elevator"],
    }
    report["groups"] = {}
    for name, models in groups.items():
        stats = {
            key: sum(report["models"][model][key] for model in models)
            for key in ("geometry_bytes", "texture_bytes", "triangles", "batches")
        }
        if (
            stats["geometry_bytes"] > 6 * 1024 * 1024
            or stats["texture_bytes"] > 8 * 1024 * 1024
            or stats["triangles"] > 15000
            or stats["batches"] > 150
        ):
            raise ValueError(name + ": combined group budget exceeded")
        report["groups"][name] = stats
    report["sha256"] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(OUT.iterdir())
        if path.name != "manifest.json"
    }
    report["disk_bytes"] = sum((OUT / name).stat().st_size for name in report["sha256"])
    (OUT / "manifest.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps(report, indent=2, sort_keys=True), flush=True)
    base.destroy()


if __name__ == "__main__":
    main()
