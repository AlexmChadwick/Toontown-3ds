#!/usr/bin/env python3
"""Reference for the Jungle Vines logic host test (source/game/vine_logic.*).

Mirrors DistributedVineGame.py / SwingVine.py / DistributedVineGameAI.py with the course read
from VineGameGlobals.CourseSections in the upstream checkout. Usage: vine_ref.py [OTT_ROOT]
"""
import ast
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

NUM_VINES = 20
VINE_SPACING = 30.0
TOP_Z = 30.0
GRAVITY = -32.0
JUMP_SPEED = 64.0
LOCAL_RADIUS = 1.5
TUBE_RADIUS = 0.5
JUMP_BUFFER = 0.5
FELL_T = 0.1
BASE_BONUS = 5.0
BONUS_PER_SECOND = 0.4


def load_course(ott):
    path = os.path.join(ott, 'toontown', 'minigame', 'VineGameGlobals.py')
    tree = ast.parse(open(path, encoding='utf-8').read())
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) \
                and node.targets[0].id == 'CourseSections':
            return ast.literal_eval(node.value)
    raise ValueError('CourseSections not found')


def build_course(sections, sec_table):
    course = []
    for s in sections:
        for length, base, period, _spider in sec_table[s]:
            course.append((float(length), float(base), float(period)))
    return course[:NUM_VINES]


def swing_angle(vine, t):
    length, base, period = vine
    deg = -90.0 - base * math.cos(2.0 * math.pi * t / period)
    return math.radians(deg)


def cable_point(index, vine, angle, t):
    length = t * vine[0]
    return (index * VINE_SPACING + length * math.cos(angle), TOP_Z + length * math.sin(angle))


def cable_distance(px, pz, index, vine, angle):
    ax, az = index * VINE_SPACING, TOP_Z
    bx, bz = cable_point(index, vine, angle, 1.0)
    dx, dz = bx - ax, bz - az
    len2 = dx * dx + dz * dz
    u = 0.0
    if len2 > 0.0:
        u = min(1.0, max(0.0, ((px - ax) * dx + (pz - az) * dz) / len2))
    ex, ez = ax + u * dx - px, az + u * dz - pz
    return math.hypot(ex, ez)


def launch(vine, angle, t, forward):
    nx, nz = -math.sin(angle), math.cos(angle)
    if not forward:
        nx, nz = -nx, -nz
    speed = JUMP_SPEED * t
    return nx * speed, nz * speed


def main():
    ott = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'ref', 'ref', 'open-toontown')
    if not os.path.isdir(os.path.join(ott, 'toontown')):
        ott = os.path.join(ROOT, 'ref', 'open-toontown')
    sec_table = load_course(ott)
    course = build_course([0, 1, 2, 3], sec_table)

    for i, v in enumerate(course[:4]):
        print(f"course {i:02d}: length={v[0]:.1f} base={v[1]:.1f} period={v[2]:.2f}")

    v0 = course[0]
    for step in range(10):
        t = step * 0.3
        a = swing_angle(v0, t)
        x, z = cable_point(0, v0, a, 1.0)
        print(f"swing step {step:02d}: t={t:5.2f} angle={a:7.3f} tip=({x:7.3f}, {z:7.3f})")

    # Jump from vine 0 at t = 0.5 with the swing at game time 1.0, forward, 40 frames of 1/30 s.
    gt = 1.0
    dt = 1.0 / 30.0
    a = swing_angle(v0, gt)
    px, pz = cable_point(0, v0, a, 0.5)
    vx, vz = launch(v0, a, 0.5, True)
    def clean(v):
        return 0.0 if abs(v) < 0.0005 else v
    print(f"launch: pos=({clean(px):.3f}, {clean(pz):.3f}) vel=({clean(vx):.3f}, {clean(vz):.3f})")
    last_jump = 0
    jump_time = gt
    caught = -1
    for frame in range(40):
        vz += GRAVITY * dt
        px += vx * dt
        pz += vz * dt
        now = gt + (frame + 1) * dt
        for j in range(max(0, last_jump - 1), min(NUM_VINES - 1, last_jump + 1) + 1):
            if j == last_jump and now - jump_time < JUMP_BUFFER:
                continue
            aj = swing_angle(course[j], now)
            if cable_distance(px, pz, j, course[j], aj) < LOCAL_RADIUS + TUBE_RADIUS:
                caught = j
                break
        print(f"jump frame {frame:02d}: pos=({px:7.3f}, {pz:7.3f}) vel=({vx:7.3f}, {vz:7.3f}) caught={caught}")
        if caught >= 0:
            break

    for treas, time_left, reached in [(0, 0.0, False), (5, 0.0, False), (10, 0.0, False),
                                      (5, 10.0, True), (10, 25.0, True), (15, 45.0, True)]:
        score = treas
        if reached and time_left > 0.0:
            bonus = max(0, int(BASE_BONUS + BONUS_PER_SECOND * time_left))
            score += bonus
        score = max(1, score)
        print(f"score: treas={treas:2d} time={time_left:4.1f} end={int(reached)} -> {score:2d}")


if __name__ == '__main__':
    main()
