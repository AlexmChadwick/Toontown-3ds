#!/usr/bin/env python3
"""Reference output for source/game/cannon_logic (host test: tools/cannon_test/run_tests.sh).

Runs upstream Python (CannonGameGlobals, Trajectory, and Panda's RandomNumGen)
for a variety of doIds, angles, and difficulties.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

# Locate open-toontown
OTT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'ref', 'open-toontown')
if not os.path.exists(os.path.join(OTT, 'toontown')):
    alt = os.path.join(ROOT, 'ref', 'ref', 'open-toontown')
    if os.path.exists(os.path.join(alt, 'toontown')):
        OTT = alt
    else:
        OTT = '/Users/alexchadwick/toontown-3ds/ref/open-toontown'

sys.path.insert(0, os.path.abspath(OTT))

from direct.showbase.RandomNumGen import RandomNumGen  # noqa: E402
from panda3d.core import Point3, Vec3  # noqa: E402
from toontown.minigame import CannonGameGlobals  # noqa: E402
from toontown.minigame.Trajectory import Trajectory  # noqa: E402

TOWER_HEIGHT = 43.85
TOWER_RADIUS = 10.5
BUCKET_HEIGHT = 36.0
TOWER_Y_RANGE = CannonGameGlobals.TowerYRange  # 200
TOWER_X_RANGE = int(TOWER_Y_RANGE / 2.0)      # 100
INITIAL_VELOCITY = 94.0
CANNON_Y = -int(CannonGameGlobals.TowerYRange / 2 * 1.3)  # -130
CANNON_Z = 20.0
CANNON_X_SPACING = 12.0
BARREL_OFFSET = 6.0
BARREL_PIVOT_Z = 1.89703

HIT_GROUND = 0
HIT_TOWER = 1
HIT_WATER = 2


def get_tower_pos(rng, diff):
    yRange = TOWER_Y_RANGE
    yMin = int(yRange * 0.3)
    yMax = yRange
    y = rng.randint(yMin, yMax)

    xRange = TOWER_X_RANGE
    x = rng.randint(0, xRange)
    x = x - int(xRange / 2.0)

    scale = 0.5 + 0.5 * diff
    x *= scale
    yCenter = (yMin + yMax) / 2.0
    y = (y - yCenter) * scale + yCenter

    x = float(x) * (float(y) / float(yRange))
    y = y - int(yRange / 2.0)
    return Point3(x, y, 0.0)


def calc_impact(start_pos, start_vel, tower_pos):
    traj = Trajectory(0.0, start_pos, start_vel)
    water_tower = [tower_pos + Point3(0, 0, BUCKET_HEIGHT), TOWER_RADIUS, TOWER_HEIGHT - BUCKET_HEIGHT]

    water_disc_center = Point3(water_tower[0])
    water_disc_center.setZ(water_disc_center[2] + water_tower[2])
    t_water = traj.checkCollisionWithDisc(water_disc_center, water_tower[1])
    if t_water > 0:
        return t_water, HIT_WATER

    t_tower = traj.checkCollisionWithCylinderSides(water_tower[0], water_tower[1], water_tower[2])
    if t_tower > 0:
        return t_tower, HIT_TOWER

    t_ground = traj.checkCollisionWithGround()
    if t_ground >= 0:
        return t_ground, HIT_GROUND
    return 0.0, HIT_GROUND


def main():
    test_cases = [
        # (doId, diff)
        (1001, 0.0),
        (2002, 0.5),
        (3003, 1.0),
        (4004, 0.25),
        (5005, 0.75),
    ]

    for doId, diff in test_cases:
        rng = RandomNumGen(doId)
        tower = get_tower_pos(rng, diff)
        print(f"tower doId={doId} diff={diff:.2f} pos=({tower[0]:.4f}, {tower[1]:.4f}, {tower[2]:.4f})")

        # Test aiming from slot 0 in 1-player game
        cannon_x = 0.0
        for z_rot in [-20.0, -10.0, 0.0, 10.0, 20.0]:
            for angle in [15.0, 30.0, 45.0, 60.0, 75.0, 85.0]:
                rot_rad = z_rot * math.pi / 180.0
                ang_rad = angle * math.pi / 180.0
                horiz_vel = INITIAL_VELOCITY * math.cos(ang_rad)
                start_vel = Vec3(horiz_vel * -math.sin(rot_rad),
                                 horiz_vel * math.cos(rot_rad),
                                 INITIAL_VELOCITY * math.sin(ang_rad))
                start_pos = Point3(cannon_x - BARREL_OFFSET * math.cos(ang_rad) * math.sin(rot_rad),
                                   CANNON_Y + BARREL_OFFSET * math.cos(ang_rad) * math.cos(rot_rad),
                                   CANNON_Z + BARREL_PIVOT_Z + BARREL_OFFSET * math.sin(ang_rad))
                t_impact, what = calc_impact(start_pos, start_vel, tower)
                score = CannonGameGlobals.calcScore(t_impact)
                print(f"shot rot={z_rot:.1f} ang={angle:.1f} hit={what} t={t_impact:.4f} score={score}")


if __name__ == '__main__':
    main()
