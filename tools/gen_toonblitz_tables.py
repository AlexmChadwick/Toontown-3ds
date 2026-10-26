#!/usr/bin/env python3
"""Generate source/game/two_d_tables.h (Toon Escape) from the upstream Python and models.

    .venv/bin/python -I tools/gen_toonblitz_tables.py > source/game/two_d_tables.h

Sources, all read at generation time (nothing is typed in by hand):
  - toontown/minigame/ToonBlitzGlobals.py: per-hood rules, BlockTypes, StomperTypes,
    SectionTypes (blocks, enemies, treasures, spawn points, stompers), BlockListStart.
  - phase_4/models/minigames/toonblitz_game_block0N.bam: each block's collision polygons,
    transformed by its BlockTypes hpr/scale exactly as TwoDBlock.createNewBlock does.
  - toonblitz_game_stomper.bam: the stomper's side walls and the block on top of it.
  - toonblitz_game_wall.bam: the start and end walls' side faces.
  - toonblitz_game_elevator.bam: loc_elevator_front / loc_elevator_inside.

Every polygon in these models spans the Y axis (y = +-1.71 or wider), so the 2D game keeps
its X/Z outline: a segment, its plane normal, and whether Panda treats it as floor
(FloorBitmask, found by the downward gravity ray) or wall (WallBitmask, pushes the
avatar's wall sphere). 'floor1' is the underside the head sphere bumps into.
"""
import ast
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))


def find_dir(marker, *candidates):
    """First candidate holding `marker` (linked worktrees reach the submodules through ref/ref)."""
    for c in candidates:
        if os.path.exists(os.path.join(c, marker)):
            return c
    raise SystemExit('missing ' + candidates[0])


UPSTREAM = find_dir('toontown', os.path.join(ROOT, 'ref', 'open-toontown'), os.path.join(ROOT, 'ref', 'ref', 'open-toontown'))
RESOURCES = find_dir('phase_4', os.path.join(ROOT, 'ref', 'resources'), os.path.join(ROOT, 'ref', 'ref', 'resources'))
MG = 'phase_4/models/minigames/'

# ToontownGlobals hood ids, in the order the tables are written.
HOODS = (('ToontownCentral', 2000), ('DonaldsDock', 1000), ('DaisyGardens', 5000),
         ('MinniesMelodyland', 4000), ('TheBrrrgh', 3000), ('DonaldsDreamland', 9000))

WALL_BIT = 1   # OTPGlobals.WallBitmask = BitMask32(0x01)
FLOOR_BIT = 2  # OTPGlobals.FloorBitmask = BitMask32(0x02)


def load_globals():
    """ToonBlitzGlobals evaluated with its two imports replaced by the hood ids."""
    path = os.path.join(UPSTREAM, 'toontown', 'minigame', 'ToonBlitzGlobals.py')
    tree = ast.parse(open(path, encoding='utf-8').read())
    body = [n for n in tree.body if not isinstance(n, (ast.Import, ast.ImportFrom))]
    module = ast.Module(body=body, type_ignores=[])

    class ToontownGlobals:
        pass

    for name, hood in HOODS:
        setattr(ToontownGlobals, name, hood)

    class Config:
        @staticmethod
        def GetBool(name, default):
            return default

    scope = {'ToontownGlobals': ToontownGlobals, 'config': Config()}
    exec(compile(module, path, 'exec'), scope)
    return scope


def f(v):
    text = '%.9g' % float(v)
    if 'e' not in text and '.' not in text:
        text += '.0'
    return text + 'f'


def vec(v):
    return '{%s}' % ', '.join(f(c) for c in v)


class Models:
    def __init__(self):
        from panda3d.core import loadPrcFileData
        loadPrcFileData('', 'window-type none\naudio-library-name null\nnotify-level error\nmodel-path ' + RESOURCES)
        from direct.showbase.ShowBase import ShowBase
        self.base = ShowBase()

    def load(self, name):
        model = self.base.loader.loadModel(MG + name + '.bam')
        if model.isEmpty():
            raise SystemExit('missing model ' + name)
        return model

    @staticmethod
    def segments(model, relative_to, kinds=None):
        """X/Z outline of every collision polygon under `model`, in `relative_to` space."""
        from panda3d.core import CollisionNode, CollisionPolygon
        out = []
        nodes = list(model.findAllMatches('**/+CollisionNode'))
        if isinstance(model.node(), CollisionNode):
            nodes.insert(0, model)
        for np in nodes:
            node = np.node()
            mask = node.getIntoCollideMask().getWord()
            name = np.getName()
            if kinds and name not in kinds:
                continue
            mat = np.getMat(relative_to)
            for i in range(node.getNumSolids()):
                solid = node.getSolid(i)
                if not isinstance(solid, CollisionPolygon):
                    raise SystemExit('unexpected solid %s in %s' % (solid.getType(), name))
                points = [mat.xformPoint(solid.getPoint(j)) for j in range(solid.getNumPoints())]
                normal = mat.xformVec(solid.getNormal())
                normal.normalize()
                if abs(normal[1]) > 1e-3:
                    continue  # faces the camera (wall fronts): never touched at y = 0
                ys = [p[1] for p in points]
                if not (min(ys) < 0 < max(ys)):
                    raise SystemExit('%s polygon does not span y = 0' % name)
                xs = sorted({round(p[0], 4) for p in points})
                zs = sorted({round(p[2], 4) for p in points})
                if abs(normal[2]) > abs(normal[0]):
                    seg = (xs[0], zs[0], xs[-1], zs[0])  # horizontal
                else:
                    seg = (xs[0], zs[0], xs[0], zs[-1])  # vertical
                kind = 'ceiling' if name == 'floor1' else ('floor' if mask & FLOOR_BIT else 'wall')
                if kind == 'floor' and not mask & FLOOR_BIT:
                    raise SystemExit('floor without FloorBitmask')
                if kind != 'floor' and not mask & WALL_BIT:
                    raise SystemExit('%s without WallBitmask' % name)
                out.append((kind, seg, (round(normal[0], 6), round(normal[2], 6))))
        return out


def main():
    g = load_globals()
    models = Models()
    from panda3d.core import NodePath

    out = []
    w = out.append
    w('// Generated by tools/gen_toonblitz_tables.py from ToonBlitzGlobals.py and the')
    w('// toonblitz_game_*.bam collision solids. Do not edit.')
    w('#pragma once')
    w('')
    w('#include <cstdint>')
    w('')
    w('namespace tt::twodgame {')
    w('')
    w('// ---- ToonBlitzGlobals scalars ----')
    w('constexpr float kShowScoresDuration = %s;' % f(g['ShowScoresDuration']))
    w('constexpr int kScoreToJellyBeansMultiplier = %d;' % g['ScoreToJellyBeansMultiplier'])
    w('constexpr int kScoreGainPerTreasure = %d;' % g['ScoreGainPerTreasure'])
    w('constexpr float kToonStartingPosition[3] = %s;' % vec(g['ToonStartingPosition']))
    w('constexpr float kCameraStartingPosition[6] = %s;' % vec(g['CameraStartingPosition']))
    w('constexpr float kBonusPerSecondLeft = %s;' % f(g['BonusPerSecondLeft']))
    w('constexpr int kDamagePerBullet = %d;' % g['DamagePerBullet'])
    w('constexpr int kEnemyBaseHealth = %d;' % g['EnemyBaseHealth'])
    w('')
    w('// Per safe zone (ToontownGlobals hood id).')
    w('struct HoodRules {')
    w('    uint32_t hood;')
    w('    float gameDuration;')
    w('    int baseBonusOnCompletion;')
    w('    int scoreLossPerEnemyCollision;')
    w('    int scoreLossPerFallDown;')
    w('    int scoreLossPerStomperSquish;')
    w('    int numSections;')
    w('};')
    w('constexpr HoodRules kHoodRules[%d] = {' % len(HOODS))
    for _, hood in HOODS:
        w('    {%d, %s, %d, %d, %d, %d, %d},' % (
            hood, f(g['GameDuration'][hood]), g['BaseBonusOnCompletion'][hood],
            g['ScoreLossPerEnemyCollision'][hood], g['ScoreLossPerFallDown'][hood],
            g['ScoreLossPerStomperSquish'][hood], g['NumSections'][hood]))
    w('};')
    w('')

    # ---- collision outlines ----
    w('enum class SegKind : uint8_t { Floor, Wall, Ceiling };')
    w('struct Segment {')
    w('    SegKind kind;')
    w('    float x1, z1, x2, z2; // x1 <= x2, z1 <= z2')
    w('    float nx, nz;         // plane normal')
    w('};')
    w('')

    block_types = g['BlockTypes']
    names = list(block_types.keys())
    w('// BlockTypes (model index from the file name suffix, then pos / hpr / scale).')
    w('enum BlockType : uint8_t {')
    for name in names:
        w('    k%s,' % name)
    w('    kBlockTypeCount')
    w('};')
    w('struct BlockTypeDef {')
    w('    uint8_t model; // toonblitz_game_block0<model>')
    w('    float pos[3], hpr[3], scale[3];')
    w('    Segment segments[4];')
    w('};')
    w('constexpr BlockTypeDef kBlockTypes[kBlockTypeCount] = {')
    block_models = {}
    for name in names:
        file_name, pos, hpr, scale = block_types[name]
        index = int(file_name[-1])
        if index not in block_models:
            block_models[index] = models.load('toonblitz_game_block0%d' % index)
        # TwoDBlock.createNewBlock: model.setScale(type scale), then the copy gets pos/hpr.
        root = NodePath('root')
        holder = root.attachNewNode('block')
        copy = block_models[index].copyTo(holder)
        copy.setScale(*scale)
        holder.setPos(*pos)
        holder.setHpr(*hpr)
        segs = Models.segments(copy, root)
        if len(segs) != 4:
            raise SystemExit('block %s has %d segments' % (name, len(segs)))
        w('    {%d, %s, %s, %s, {' % (index, vec(pos), vec(hpr), vec(scale)))
        for kind, s, n in segs:
            w('        {SegKind::%s, %s, %s, %s, %s, %s, %s},' % (kind.capitalize(), f(s[0]), f(s[1]), f(s[2]), f(s[3]),
                                                             f(n[0]), f(n[1])))
        w('    }}, // %s' % name)
        root.removeNode()
    w('};')
    w('')

    # ---- stompers ----
    w('// StomperTypes: motion type, scale, head start/end z, shaft start/end scale z, coll solids.')
    w('struct StomperTypeDef {')
    w('    int motionType;')
    w('    float scale;')
    w('    float headStartZ, headEndZ;')
    w('    float shaftStartScaleZ, shaftEndScaleZ;')
    w('    int numCollSolids;')
    w('};')
    stomper_types = g['StomperTypes']
    max_type = max(stomper_types)
    w('constexpr int kNumStomperTypes = %d; // indexed by type id; entry 0 unused' % (max_type + 1))
    w('constexpr StomperTypeDef kStomperTypes[kNumStomperTypes] = {')
    for t in range(max_type + 1):
        if t not in stomper_types:
            w('    {0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0},')
            continue
        motion, scale, head, shaft, colls = stomper_types[t]
        w('    {%d, %s, %s, %s, %s, %s, %d},' % (motion, f(scale), f(head[0]), f(head[1]), f(shaft[0]), f(shaft[1]), colls))
    w('};')
    stomper = models.load('toonblitz_game_stomper')
    coll = Models.segments(stomper.find('**/stomper_collision'), stomper)
    xs = sorted(s[1][0] for s in coll)
    zs = sorted({s[1][1] for s in coll} | {s[1][3] for s in coll})
    w('// stomper_collision: two side walls at x = +-halfWidth from z 0 to height (model units).')
    w('constexpr float kStomperCollHalfWidth = %s;' % f(xs[-1]))
    w('constexpr float kStomperCollHeight = %s;' % f(zs[-1] - zs[0]))
    group = stomper.find('**/block_group')
    w('// block_group offset (scaled with the stomper; the block itself is unscaled).')
    w('constexpr float kStomperBlockZ = %s;' % f(group.getZ(stomper)))
    block = stomper.find('**/block')
    block_root = NodePath('stomper_block')
    block.copyTo(block_root)
    block_segs = Models.segments(block_root, block_root)
    w('constexpr Segment kStomperBlockSegments[%d] = {' % len(block_segs))
    for kind, s, n in block_segs:
        w('    {SegKind::%s, %s, %s, %s, %s, %s, %s},' % (kind.capitalize(), f(s[0]), f(s[1]), f(s[2]), f(s[3]),
                                                      f(n[0]), f(n[1])))
    w('};')
    w('')

    # ---- start / end walls and the elevator ----
    wall = models.load('toonblitz_game_wall')
    wall_segs = Models.segments(wall, wall)
    w('// toonblitz_game_wall side faces (model units, before TwoDSectionMgr scales it 0.8).')
    w('constexpr Segment kWallSegments[%d] = {' % len(wall_segs))
    for kind, s, n in wall_segs:
        w('    {SegKind::%s, %s, %s, %s, %s, %s, %s},' % (kind.capitalize(), f(s[0]), f(s[1]), f(s[2]), f(s[3]),
                                                      f(n[0]), f(n[1])))
    w('};')
    elevator = models.load('toonblitz_game_elevator')
    w('constexpr float kElevatorFront[3] = %s; // loc_elevator_front' % vec(elevator.find('**/loc_elevator_front').getPos(elevator)))
    w('constexpr float kElevatorInside[3] = %s; // loc_elevator_inside' % vec(elevator.find('**/loc_elevator_inside').getPos(elevator)))
    w('constexpr float kElevatorDoorL[3] = %s; // doorL' % vec(elevator.find('**/doorL').getPos(elevator)))
    w('constexpr float kElevatorDoorR[3] = %s; // doorR' % vec(elevator.find('**/doorR').getPos(elevator)))
    w('')

    # ---- sections ----
    w('struct BlockDef {')
    w('    BlockType type;')
    w('    bool moving;')
    w('    float init[3], final[3];')
    w('    float duration; // one way; the LerpPosInterval pair loops forward then back')
    w('};')
    w('struct EnemyDef {')
    w('    char suit[4];')
    w('    bool moving;')
    w('    float init[3], final[3];')
    w('    float duration;')
    w('};')
    w('struct SpawnPointDef {')
    w('    float save[3], load[3];')
    w('};')
    w('struct StomperDef {')
    w('    uint8_t type;')
    w('    float pos[3];')
    w('    float period;')
    w('};')
    w('struct SectionTypeDef {')
    w('    int difficulty;')
    w('    float length;')
    w('    const BlockDef* blocks;')
    w('    int numBlocks;')
    w('    const EnemyDef* enemies;')
    w('    int numEnemies;')
    w('    const float (*treasures)[3];')
    w('    int numTreasures;')
    w('    const SpawnPointDef* spawnPoints;')
    w('    int numSpawnPoints;')
    w('    const StomperDef* stompers;')
    w('    int numStompers;')
    w('};')
    w('')

    def block_def(b):
        if len(b) != 2:
            raise SystemExit('block with an hpr track: %r' % (b,))
        p = b[1]
        if len(p) == 3:
            return '{k%s, true, %s, %s, %s}' % (b[0], vec(p[0]), vec(p[1]), f(p[2]))
        return '{k%s, false, %s, %s, 0.0f}' % (b[0], vec(p[0]), vec(p[0]))

    def enemy_def(e):
        p = e[1]
        if len(p) == 3:
            return '{"%s", true, %s, %s, %s}' % (e[0], vec(p[0]), vec(p[1]), f(p[2]))
        return '{"%s", false, %s, %s, 0.0f}' % (e[0], vec(p[0]), vec(p[0]))

    w('// BlockListStart (TwoDSectionMgr.setupStartSection).')
    start = g['BlockListStart']
    w('constexpr BlockDef kStartBlocks[%d] = {' % len(start))
    for b in start:
        w('    %s,' % block_def(b))
    w('};')
    w('')

    section_types = g['SectionTypes']
    keys = sorted(k for k in section_types if k != 'end')
    if keys != list(range(len(keys))):
        raise SystemExit('SectionTypes keys are not 0..n-1')
    maxima = {'enemies': 0, 'treasures': 0, 'spawns': 0, 'stompers': 0, 'blocks': 0}

    def emit_section(tag, attribs):
        difficulty, length, blocks, enemies, treasures, spawns, stompers = attribs[:7]
        maxima['blocks'] = max(maxima['blocks'], len(blocks))
        w('constexpr BlockDef kSection%sBlocks[%d] = {' % (tag, len(blocks)))
        for b in blocks:
            w('    %s,' % block_def(b))
        w('};')
        refs = ['kSection%sBlocks' % tag, str(len(blocks))]
        for label, items, emit, key in (
                ('Enemies', enemies, lambda e: enemy_def(e), 'enemies'),
                ('Treasures', treasures, lambda t: vec(t[0]), 'treasures'),
                ('SpawnPoints', spawns, lambda s: '{%s, %s}' % (vec(s[0]), vec(s[-1])), 'spawns'),
                ('Stompers', stompers, lambda s: '{%d, %s, %s}' % (s[0], vec(s[1]), f(s[2])), 'stompers')):
            if not items:
                refs += ['nullptr', '0']
                continue
            maxima[key] = max(maxima[key], len(items))
            ctype = {'Enemies': 'EnemyDef', 'Treasures': 'float', 'SpawnPoints': 'SpawnPointDef',
                     'Stompers': 'StomperDef'}[label]
            suffix = '[3]' if label == 'Treasures' else ''
            w('constexpr %s kSection%s%s[%d]%s = {' % (ctype, tag, label, len(items), suffix))
            for item in items:
                w('    %s,' % emit(item))
            w('};')
            refs += ['kSection%s%s' % (tag, label), str(len(items))]
        return '{%d, %s, %s}' % (difficulty, f(length), ', '.join(refs))

    rows = []
    for key in keys:
        rows.append(emit_section(str(key), section_types[key]))
    end_row = emit_section('End', section_types['end'])
    w('')
    w('constexpr int kNumSectionTypes = %d;' % len(keys))
    w('constexpr SectionTypeDef kSectionTypes[kNumSectionTypes] = {')
    for key, row in zip(keys, rows):
        w('    %s, // %d' % (row, key))
    w('};')
    w('constexpr SectionTypeDef kEndSection = %s;' % end_row)
    w('')
    w('// Largest pools over every section (sizes of the AI selection arrays).')
    w('constexpr int kMaxSectionBlocks = %d;' % maxima['blocks'])
    w('constexpr int kMaxSectionEnemies = %d;' % maxima['enemies'])
    w('constexpr int kMaxSectionTreasures = %d;' % maxima['treasures'])
    w('constexpr int kMaxSectionSpawnPoints = %d;' % maxima['spawns'])
    w('constexpr int kMaxSectionStompers = %d;' % maxima['stompers'])
    w('')
    w('} // namespace tt::twodgame')
    sys.stdout.write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
