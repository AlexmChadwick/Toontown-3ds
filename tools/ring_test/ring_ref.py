#!/usr/bin/env python3
"""Reference output for source/game/ring_logic (host test: tools/ring_test/run_tests.sh).

Runs upstream Ring game difficulty patterns, RNG selection, and collision math.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

OTT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'ref', 'open-toontown')
if not os.path.exists(os.path.join(OTT, 'toontown')):
    alt = os.path.join(ROOT, 'ref', 'ref', 'open-toontown')
    if os.path.exists(os.path.join(alt, 'toontown')):
        OTT = alt
    else:
        OTT = '/Users/alexchadwick/toontown-3ds/ref/open-toontown'

sys.path.insert(0, os.path.abspath(OTT))

from direct.showbase.RandomNumGen import RandomNumGen  # noqa: E402

TTC = 2000
DD = 1000
DG = 5000
MM = 4000
TB = 3000
DDL = 9000


def upstreamPatterns():
    """DistributedRingGame.__generateRings' difficultyPatterns, read from the source."""
    import ast
    src = open(os.path.join(OTT, 'toontown/minigame/DistributedRingGame.py')).read()
    node = next(n for n in ast.walk(ast.parse(src)) if isinstance(n, ast.Assign) and len(n.targets) == 1
                and isinstance(n.targets[0], ast.Name) and n.targets[0].id == 'difficultyPatterns')
    g = type('ToontownGlobals', (), dict(ToontownCentral=TTC, DonaldsDock=DD, DaisyGardens=DG,
                                         MinniesMelodyland=MM, TheBrrrgh=TB, DonaldsDreamland=DDL))
    return eval(compile(ast.Expression(node.value), 'difficultyPatterns', 'eval'), {'ToontownGlobals': g})


difficultyPatterns = upstreamPatterns()

MAX_TOONXZ = 10.0
RING_RADIUS = MAX_TOONXZ / 3.0 * 0.9  # 3.0
ringRadius = RING_RADIUS * 1.025      # 3.075
RING_RADIUS_SQRD = ringRadius * ringRadius  # 9.455625


def main():
    hoods = [TTC, DD, DG, MM, TB, DDL]

    for hood in hoods:
        for seed in [12345, 67890, 54321]:
            rng = RandomNumGen(seed)
            pattern = rng.choice(difficultyPatterns[hood])
            pat_str = ",".join(str(x) for x in pattern)
            print(f"pattern hood={hood} seed={seed} pat=[{pat_str}]")

    # Test collision detection
    test_points = [
        (0.0, 0.0, 0.0, 0.0),
        (1.0, 2.0, 1.0, 2.0),
        (0.0, 0.0, 2.0, 2.0),
        (0.0, 0.0, 3.0, 0.0),
        (0.0, 0.0, 3.075, 0.0),
        (0.0, 0.0, 3.1, 0.0),
        (-2.0, 2.0, 0.0, 0.0),
        (5.0, 5.0, 0.0, 0.0),
    ]

    for tx, tz, rx, rz in test_points:
        dx = tx - rx
        dz = tz - rz
        d2 = dx * dx + dz * dz
        hit = 1 if d2 <= RING_RADIUS_SQRD else 0
        print(f"collide toon=({tx:.2f},{tz:.2f}) ring=({rx:.2f},{rz:.2f}) d2={d2:.4f} hit={hit}")


if __name__ == '__main__':
    main()
