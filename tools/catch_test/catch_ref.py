#!/usr/bin/env python3
"""Reference output for source/game/catch_logic (host test: tools/catch_test/run_tests.sh).

Runs the *upstream* Python (CatchGameGlobals, DropScheduler, DropPlacer, and Panda's
RandomNumGen) for a list of (doId, hoodId, numPlayers, difficulty) cases and prints one
line per drop and per suit walk. The C++ port must print the identical text.

    .venv/bin/python -I tools/catch_test/catch_ref.py ref/open-toontown > ref.txt

Only calcDifficultyConstants / scheduleDrops / startSuitWalkTask are re-typed here (they
live in DistributedCatchGame.py, which imports the whole client); every formula and RNG
call is a line-for-line copy of that file, and the placer/scheduler are imported as is.
"""
import math
import os
import sys
import types

ROOT = sys.argv[1] if len(sys.argv) > 1 else 'ref/open-toontown'
sys.path.insert(0, os.path.abspath(ROOT))

# DropPlacer imports toontown.parties.PartyGlobals only for a constant; keep the import
# light (the real module drags in most of the client).
party = types.ModuleType('toontown.parties.PartyGlobals')
party.CatchActivityDuration = 55
sys.modules['toontown.parties'] = types.ModuleType('toontown.parties')
sys.modules['toontown.parties.PartyGlobals'] = party

import builtins  # noqa: E402
from functools import reduce  # noqa: E402


class _Config:  # the `config` builtin ShowBase installs; defaults only
    def GetBool(self, name, default=False):
        return default


builtins.config = _Config()

from direct.showbase.RandomNumGen import RandomNumGen  # noqa: E402
from toontown.minigame import CatchGameGlobals  # noqa: E402
from toontown.minigame.DropPlacer import RegionDropPlacer  # noqa: E402
from toontown.minigame.DropScheduler import DropScheduler  # noqa: E402


def lerp(a, b, t):
    return a + ((b - a) * t)


class Game:
    """Just the attributes DistributedCatchGame.defineConstants/calcDifficultyConstants set."""

    class _N:
        def debug(self, *a):
            pass

    notify = _N()

    def __init__(self, doId, numPlayers, difficulty):
        self.numPlayers = numPlayers
        self.difficulty = difficulty
        self.randomNumGen = RandomNumGen(doId)
        self.DropPlacerType = RegionDropPlacer
        self.FirstDropDelay = 0.5
        self.FasterDropDelay = int(2.0 / 3 * CatchGameGlobals.GameDuration)
        self.FasterDropPeriodMult = 0.5
        self.calc(difficulty, numPlayers)
        self.DropRows, self.DropColumns = [[5, 5], [5, 5], [6, 6], [7, 7]][numPlayers - 1]

    def getDifficulty(self):
        return self.difficulty

    def getNumPlayers(self):
        return self.numPlayers

    def calc(self, difficulty, numPlayers):
        ToonSpeedRange = [16.0, 25.0]
        self.ToonSpeed = lerp(ToonSpeedRange[0], ToonSpeedRange[1], difficulty)
        self.SuitSpeed = self.ToonSpeed / 2.0
        self.SuitPeriodRange = [lerp(5.0, 3.0, self.getDifficulty()), lerp(15.0, 8.0, self.getDifficulty())]

        def scaledDimensions(widthHeight, scale):
            w, h = widthHeight
            return [math.sqrt(scale * w * w), math.sqrt(scale * h * h)]

        BaseStageDimensions = [20, 15]
        areaScales = [1.0, 1.0, 3.0 / 2, 4.0 / 2]
        self.StageAreaScale = areaScales[numPlayers - 1]
        self.StageLinearScale = math.sqrt(self.StageAreaScale)
        self.StageDimensions = scaledDimensions(BaseStageDimensions, self.StageAreaScale)
        self.StageHalfWidth = self.StageDimensions[0] / 2.0
        self.StageHalfHeight = self.StageDimensions[1] / 2.0
        MOHs = [24] * 2 + [26, 28]
        self.MinOffscreenHeight = MOHs[self.getNumPlayers() - 1]
        distance = math.sqrt(self.StageDimensions[0] * self.StageDimensions[0] +
                             self.StageDimensions[1] * self.StageDimensions[1])
        distance /= self.StageLinearScale
        ToonRunDuration = distance / self.ToonSpeed
        offScreenOnScreenRatio = 1.0
        fraction = 1.0 / 3 * 0.85
        self.BaselineOnscreenDropDuration = ToonRunDuration / (fraction * (1.0 + offScreenOnScreenRatio))
        self.OffscreenTime = offScreenOnScreenRatio * self.BaselineOnscreenDropDuration
        self.BaselineDropDuration = self.BaselineOnscreenDropDuration + self.OffscreenTime
        self.MaxDropDuration = self.BaselineDropDuration
        self.DropPeriod = self.BaselineDropDuration / 2.0
        scaledNumPlayers = (numPlayers - 1.0) * 0.75 + 1.0
        self.DropPeriod /= scaledNumPlayers
        typeProbs = {'fruit': 3, 'anvil': 1}
        probSum = reduce(lambda x, y: x + y, list(typeProbs.values()))
        for key in list(typeProbs.keys()):
            typeProbs[key] = float(typeProbs[key]) / probSum
        scheduler = DropScheduler(CatchGameGlobals.GameDuration, self.FirstDropDelay, self.DropPeriod,
                                  self.MaxDropDuration, self.FasterDropDelay, self.FasterDropPeriodMult)
        self.totalDrops = 0
        while not scheduler.doneDropping():
            scheduler.stepT()
            self.totalDrops += 1
        self.numFruits = int(self.totalDrops * typeProbs['fruit'])
        self.numAnvils = int(self.totalDrops - self.numFruits)


def run_case(doId, hood, numPlayers, difficulty):
    g = Game(doId, numPlayers, difficulty)
    out = []
    out.append('case doId=%d hood=%d players=%d diff=%.6f' % (doId, hood, numPlayers, difficulty))
    out.append('const toonSpeed=%.6f halfW=%.6f halfH=%.6f onscreen=%.6f offscreen=%.6f period=%.6f '
               'drops=%d fruits=%d anvils=%d' % (g.ToonSpeed, g.StageHalfWidth, g.StageHalfHeight,
                                                 g.BaselineOnscreenDropDuration, g.OffscreenTime,
                                                 g.DropPeriod, g.totalDrops, g.numFruits, g.numAnvils))
    out.append('serverFruits=%d' % CatchGameGlobals.NumFruits[numPlayers - 1][hood])
    names = ['fruit'] * g.numFruits + ['anvil'] * g.numAnvils
    g.randomNumGen.shuffle(names)
    placer = g.DropPlacerType(g, numPlayers, names)
    schedule = []
    while not placer.doneDropping():
        schedule.append(placer.getNextDrop())
    for i, (t, name, coords) in enumerate(schedule):
        out.append('drop %d t=%.6f %s %d %d' % (i, t, name, coords[0], coords[1]))
    # enterPlay: scheduleDrops, then startSuitWalkTask seeds its sub-generator from randomNumGen ...
    rng = RandomNumGen(g.randomNumGen)
    # ... and only then do objects spawn, each drawing one random() (getDropIval: newH / delta).
    for i, (t, name, coords) in enumerate(schedule):
        out.append('spawnrand %d %.9f' % (i, g.randomNumGen.random()))
    delay = 0.0
    k = 0
    walks = []
    while delay < CatchGameGlobals.GameDuration:
        delay += lerp(g.SuitPeriodRange[0], g.SuitPeriodRange[0], rng.random())

        def pickY():
            return lerp(-g.StageHalfHeight, g.StageHalfHeight, rng.random())

        m = [2.5, 2.5, 2.3, 2.1][numPlayers - 1]
        sx0, sy0 = -(g.StageHalfWidth * m), pickY()
        sx1, sy1 = g.StageHalfWidth * m, pickY()
        if rng.choice([0, 1]):
            sx0, sy0, sx1, sy1 = sx1, sy1, sx0, sy0
        dur = math.sqrt((sx1 - sx0) ** 2 + (sy1 - sy0) ** 2) / g.SuitSpeed
        walks.append([delay, sx0, sy0, sx1, sy1, dur, -1])
        k += 1
    # getSuitWalkIval.setup/cleanup run in time order: rng.choice(self.suits), remove; cleanup appends.
    free = [0, 1, 2, 3]
    started = [False] * len(walks)
    finished = [False] * len(walks)
    while True:
        best, is_setup, best_t = -1, True, 0.0
        for i, w in enumerate(walks):
            if not started[i] and (best < 0 or w[0] < best_t):
                best, is_setup, best_t = i, True, w[0]
            if started[i] and not finished[i] and (best < 0 or w[0] + w[5] < best_t):
                best, is_setup, best_t = i, False, w[0] + w[5]
        if best < 0:
            break
        if is_setup:
            started[best] = True
            if not free:
                finished[best] = True
                continue
            suit = rng.choice(free)
            walks[best][6] = suit
            free.remove(suit)
        else:
            finished[best] = True
            if walks[best][6] >= 0:
                free.append(walks[best][6])
    for k, w in enumerate(walks):
        out.append('suit %d delay=%.6f from=%.6f,%.6f to=%.6f,%.6f dur=%.6f kind=%d' % (k, *w))
    return out


CASES = [
    (100000001, 2000, 1, 0.0),
    (100000417, 2000, 1, 0.0),
    (555555555, 1000, 1, 0.2),
    (123456789, 5000, 2, 0.4),
    (987654321, 4000, 3, 0.6),
    (424242424, 3000, 4, 0.8),
    (31337, 9000, 1, 1.0),
    (2147483000, 2000, 2, 0.0),
]

if __name__ == '__main__':
    for c in CASES:
        print('\n'.join(run_case(*c)))
    # AI scoring spot checks (DistributedCatchGameAI.gameOver), mirrored by catch_test.cpp.
    def ai_score(fruits, num):
        score = max(1, int((1 + fruits) / 2))
        if fruits >= num:
            score += round(num / 4.0)
        return score

    print('score 0/18=%d 5/18=%d 17/18=%d 18/18=%d 28/28=%d 19/19=%d' % (
        ai_score(0, 18), ai_score(5, 18), ai_score(17, 18), ai_score(18, 18), ai_score(28, 28), ai_score(19, 19)))
