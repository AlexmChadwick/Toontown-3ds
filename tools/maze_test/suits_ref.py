"""Stock MazeSuit tile walk for suits_test.cpp (the Cog actor stubbed; needs panda3d.otp: tools/server/.venv)."""
import sys, types, builtins, importlib.util
OTT = sys.argv[1]; sys.path.insert(0, OTT)
class Cfg:
    def GetBool(self, *a): return a[1] if len(a) > 1 else 0
builtins.config = Cfg(); builtins.render = None
# Stub the Cog actor: only the seeded tile walk is compared.
class FakeSuit:
    def __getattr__(self, n): return lambda *a, **k: None
suitmod = types.ModuleType('toontown.suit.Suit'); suitmod.Suit = FakeSuit
dnamod = types.ModuleType('toontown.suit.SuitDNA')
class FakeDNA:
    def newSuit(self, n): pass
dnamod.SuitDNA = FakeDNA
pkg = types.ModuleType('toontown.suit'); pkg.Suit = suitmod; pkg.SuitDNA = dnamod; pkg.__path__ = []
sys.modules['toontown.suit'] = pkg; sys.modules['toontown.suit.Suit'] = suitmod; sys.modules['toontown.suit.SuitDNA'] = dnamod
from direct.showbase.RandomNumGen import RandomNumGen
from toontown.minigame import MazeData
from toontown.minigame.MazeSuit import MazeSuit
from toontown.minigame.MazeBase import MazeBase
import ast
src = open(OTT + '/toontown/minigame/DistributedMazeGame.py').read()
tabs = {}
for n in ast.walk(ast.parse(src)):
    if isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Attribute) and isinstance(n.value, ast.Dict):
        try: tabs[n.targets[0].attr] = ast.literal_eval(n.value)
        except ValueError: pass
class Maze(MazeBase):
    def __init__(self, name):
        d = MazeData.mazeData[name]
        self.width, self.height = d['width'], d['height']; self.originTX, self.originTY = d['originX'], d['originY']
        self.collisionTable = d['collisionTable']; self.cellWidth = MazeData.CELL_WIDTH
class S(MazeSuit):
    def gameStart(self, t):
        self.gameStartTime = t; self.occupiedTiles = [(self.nextTX, self.nextTY)]
        self.nextThinkTic = self.serialNum * self.ticFreq // 20
    def getThinkTimestampTics(self, curTic):
        r = MazeSuit.getThinkTimestampTics(self, curTic); self.lastTicBeforeRender = -99; return r
for seed in (12345, 4000123):
    for hood in (2000, 9000):
        for np_ in range(1, 5):
            maze = Maze(MazeData.mazeNames[np_ - 1][0])
            rng = RandomNumGen(seed)
            starts = [0, 1, 2, 3]; rng.shuffle(starts)
            toon = [RandomNumGen(rng) for i in range(np_)]
            diff = 0.0 if hood == 2000 else 1.0
            ns = 4 * np_
            slower = tabs['slowerSuitPeriodsCurve' if diff < 0.5 else 'slowerSuitPeriods'][hood][ns]
            faster = tabs['fasterSuitPeriodsCurve'][hood][ns]
            per = slower + faster; rng.shuffle(per)
            suits = [S(i, maze, rng, per[i], diff) for i in range(ns)]
            for s in suits: s.onstage(); s.gameStart(0)
            print('s%d h%d n%d starts %s' % (seed, hood, np_, ' '.join(map(str, starts))))
            for step in range(1, 61):
                t = step * 0.37
                # thinkSuits with curT = t (globalClock stubbed)
                class C:
                    def getFrameTime(self): return t
                builtins.globalClock = C()
                MazeSuit.thinkSuits(suits, 0)
                if step % 10 == 0:
                    print('s%d h%d n%d t%d:' % (seed, hood, np_, step) + ''.join(' %d,%d,%d' % (s.TX, s.TY, s.direction) for s in suits))
