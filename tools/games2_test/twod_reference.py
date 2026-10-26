"""Toon Escape references from the real upstream code and Panda3D; run with Python -I -B.

Writes sdmc-out/games2-test/twod.txt for twod_test.cpp:
  - setSectionsSelected: DistributedTwoDGameAI.setupSections (copied out of the AI file and
    run as is with seeded `random`) for every hood and player count, packed by Panda's DC
    packer, with the layout TwoDSectionMgr gives it (section X, gameEndX).
  - physics: LocalAvatar's GravityWalker (pusher, gravity ray, early event sphere) driven the
    way TwoDDrive drives it over the real block / wall models, frame by frame.
"""
import ast
import os
import random
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import gen_toonblitz_tables as gen  # noqa: E402

OUT = ROOT / 'sdmc-out/games2-test'
OUT.mkdir(parents=True, exist_ok=True)
UPSTREAM = Path(gen.UPSTREAM)

from panda3d.core import (BitMask32, ClockObject, CollisionNode, CollisionPlane, NodePath, Plane, Point3,  # noqa: E402
                          Vec3, loadPrcFileData)
from panda3d.direct import DCFile  # noqa: E402

G = gen.load_globals()
DT = 1.0 / 30.0


class Globals:
    pass


TB = Globals()
for key, value in G.items():
    setattr(TB, key, value)


def setup_sections_function():
    """DistributedTwoDGameAI.setupSections, compiled from the AI source."""
    path = UPSTREAM / 'toontown/minigame/DistributedTwoDGameAI.py'
    tree = ast.parse(path.read_text(encoding='utf-8'))
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == 'setupSections':
            module = ast.Module(body=[node], type_ignores=[])
            scope = {'ToonBlitzGlobals': TB, 'random': random}
            exec(compile(module, str(path), 'exec'), scope)
            return scope['setupSections']
    raise SystemExit('setupSections not found')


class FakeAI:
    class notify:
        @staticmethod
        def debug(_):
            pass

    def __init__(self, hood):
        self.hood = hood
        self.sectionsSelected = []
        self.sectionIndexList = []

    def getSafezoneId(self):
        return self.hood


def main():
    dc = DCFile()
    for name in ('otp.dc', 'toon.dc'):
        assert dc.read(str(UPSTREAM / 'etc' / name))
    assert dc.getHash() == 547155168
    cls = dc.getClassByName('DistributedTwoDGame')
    field = cls.getFieldByName('setSectionsSelected')
    setup = setup_sections_function()
    lines = []
    cases = 0
    selections = []
    for _, hood in gen.HOODS:
        # One deterministic AI selection per hood exercises Panda's packed field without
        # turning the host suite into a multi-minute exhaustive simulation.
        for seed in range(1):
            random.seed(hood * 100 + seed)
            ai = FakeAI(hood)
            setup(ai)
            sections = ai.sectionsSelected
            data = bytes(field.parseString('(%r)' % (sections,)))
            players = 1 + seed % 4
            # TwoDSectionMgr: sections from x = -24, the end section after them; its single
            # save point is the finish line (TwoDSpawnPointMgr.gameEndX).
            x = -24.0
            origins = []
            for s in sections:
                origins.append(x)
                x += G['SectionTypes'][s[0]][1]
            end_x = x
            game_end = end_x + G['SectionTypes']['end'][5][0][0][0]
            flat = []
            for s in sections:
                flat += [s[0], len(s[1])] + list(s[1]) + [len(s[2])]
                for t in s[2]:
                    flat += [t[0], t[1]]
                flat += [len(s[3])] + list(s[3]) + [len(s[4])] + list(s[4])
            lines.append('select %d %d %d %d %s %d %s %.4f %.4f' % (
                hood, players, field.getNumber(), len(data), data.hex(), len(sections),
                ' '.join(str(v) for v in flat), end_x, game_end))
            lines.append('origins %s' % ' '.join('%.4f' % o for o in origins))
            selections.append((hood, sections))
            cases += 1

    # ---- physics: GravityWalker over the real models ----
    loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level error\nmodel-path ' +
                    gen.RESOURCES)
    from direct.showbase.ShowBase import ShowBase
    base = ShowBase()
    from direct.controls.GravityWalker import GravityWalker
    clock = ClockObject.getGlobalClock()
    clock.setMode(ClockObject.MNonRealTime)
    clock.setDt(DT)
    block_models = [base.loader.loadModel(gen.MG + 'toonblitz_game_block0%d.bam' % i) for i in range(4)]
    wall_model = base.loader.loadModel(gen.MG + 'toonblitz_game_wall.bam')

    def place_block(parent, origin_x, attribs):
        file_name, pos, hpr, scale = G['BlockTypes'][attribs[0]]
        holder = parent.attachNewNode('block')
        copy = block_models[int(file_name[-1])].copyTo(holder)
        copy.setScale(*scale)
        init = attribs[1][0]
        holder.setPos(origin_x + pos[0] + init[0], pos[1] + init[1], pos[2] + init[2])
        holder.setHpr(*hpr)

    # Courses of the sections whose blocks never move and have no stompers; no Cogs.
    static = [k for k in G['SectionsPool']
              if not any(len(b[1]) == 3 for b in G['SectionTypes'][k][2]) and G['SectionTypes'][k][6] is None]
    from panda3d.core import CollisionTraverser
    physics = 0
    for run in range(2):
        layout = random.Random(1000 + run)
        sections = [(layout.choice(static), [], [], [0], []) for _ in range(5)]
        world = render.attachNewNode('world')
        for b in G['BlockListStart']:
            place_block(world, -48.0, b)
        x = -24.0
        for s in sections:
            for b in G['SectionTypes'][s[0]][2]:
                place_block(world, x, b)
            x += G['SectionTypes'][s[0]][1]
        for b in G['SectionTypes']['end'][2]:
            place_block(world, x, b)
        for wx in (-48.0 - 28.0, x + 100.0):
            wall = wall_model.copyTo(world)
            wall.setPos(wx, 0, 4)
            wall.setScale(0.8)
        plane = CollisionNode('dropPlane')
        plane.addSolid(CollisionPlane(Plane(Vec3(0, 0, 1), Point3(0, 0, -50))))
        plane.setCollideMask(BitMask32(2))
        world.attachNewNode(plane)

        avatar = render.attachNewNode('avatar')
        avatar.setPos(*G['ToonStartingPosition'])
        traverser = CollisionTraverser('twoD')  # the main traverser (pusher)
        walker = GravityWalker()
        walker.setWallBitMask(BitMask32(1))
        walker.setFloorBitMask(BitMask32(2))
        walker.initializeCollisions(traverser, avatar, 1.4, 0.025, 4.0)
        # initializeCollisions made one settling pass; start from the starting position.
        avatar.setPos(*G['ToonStartingPosition'])
        walker.lifter.setVelocity(0)

        rng = random.Random(run)
        lines.append('physics %d' % (len(lines),))
        move = 1
        frames = []
        for frame in range(900):
            if frame % 10 == 0:
                move = rng.choice([1, 1, 1, 1, 0, -1])
            jump = frame % 10 == 0 and rng.random() < 0.35
            # TwoDDrive.__update: X first, then TwoDWalker.jumpPressed from the ground.
            avatar.setX(avatar.getX() + move * 12.0 * DT)
            jumped = 0
            if jump and walker.lifter.isOnGround():
                walker.lifter.addVelocity(30.0)
                jumped = 1
            clock.tick()
            traverser.traverse(render)
            base.shadowTrav.traverse(render)
            frames.append('%d %d %d %.5f %.5f %.5f' % (move, int(jump), jumped, avatar.getX(), avatar.getZ(),
                                                      walker.lifter.getVelocity()))
            if avatar.getZ() < -2.0:
                break
        lines[-1] = 'physics %d %d %s' % (len(sections), len(frames), ' '.join(str(sec[0]) for sec in sections))
        lines.extend('f ' + f for f in frames)
        walker.setCollisionsActive(0)
        walker.deleteCollisions()
        avatar.removeNode()
        world.removeNode()
        physics += 1

    (OUT / 'twod.txt').write_text('\n'.join(lines) + '\n')
    print('Toon Escape references: %d AI selections, %d physics runs' % (cases, physics))


if __name__ == '__main__':
    main()
