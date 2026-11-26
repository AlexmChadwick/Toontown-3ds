"""Stock RingTrackGroups positions for tracks_test.cpp (needs a Python with panda3d.otp: tools/server/.venv)."""
import sys, builtins, ast
OTT = sys.argv[1]
sys.path.insert(0, OTT)
from panda3d.core import loadPrcFileData
loadPrcFileData('', 'notify-level error\naudio-library-name null\nwindow-type none')
class Cfg:
    def GetBool(self, *a): return a[1] if len(a) > 1 else 0
    def GetInt(self, *a): return a[1] if len(a) > 1 else 0
    def GetString(self, *a): return a[1] if len(a) > 1 else ''
    def GetFloat(self, *a): return a[1] if len(a) > 1 else 0.0
builtins.config = Cfg()
from direct.showbase.RandomNumGen import RandomNumGen
from toontown.minigame import RingTrackGroups
src = open(OTT + '/toontown/minigame/DistributedRingGame.py').read()
node = next(n for n in ast.walk(ast.parse(src)) if isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name) and n.targets[0].id == 'difficultyPatterns')
from toontown.toonbase import ToontownGlobals
pats = eval(compile(ast.Expression(node.value), 'p', 'eval'), {'ToontownGlobals': ToontownGlobals})
for h in (2000, 1000, 5000, 4000, 3000, 9000):
    for s in (12345, 67890, 1000123, 77):
        for n in range(1, 5):
            rng = RandomNumGen(s)
            pattern = rng.choice(pats[h])
            groups = [RingTrackGroups.getRandomRingTrackGroup(pattern[i], n, rng) for i in range(16)]
            for i, g in enumerate(groups):
                for r in range(n):
                    out = []
                    for t in (0.0, 1.3, 2.7, 4.1, 9.9):
                        normT = (t / g.period + g.tOffset) % 1.0
                        if g.reverseFlag: normT = 1.0 - normT
                        p = g.tracks[r].eval((normT + g.trackTOffsets[r]) % 1.0)
                        out.append('%.3f,%.3f' % (p[0] * 10.0 + 0.0, p[1] * 10.0 + 0.0))
                    print('h%d s%d n%d g%d r%d: %s' % (h, s, n, i, r, ' '.join(out)))
