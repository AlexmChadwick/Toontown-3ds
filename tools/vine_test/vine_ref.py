#!/usr/bin/env python3
"""Reference generator for Jungle Vines minigame logic host test."""
import math

def main():
    kVineSpacing = 30.0
    kVineHeight = 30.0
    kVineTopZ = 30.0
    kGravity = -32.0
    kBaseBonus = 5.0
    kBonusPerSecond = 0.4

    # 1. Swing test over 20 time steps
    for step in range(20):
        t = step * 0.15
        omega = (2.0 * math.pi) / 3.0
        angle = 0.785398 * math.sin(omega * t)
        length = 0.5 * kVineHeight
        x = 0.0 + length * math.sin(angle)
        z = kVineTopZ - length * math.cos(angle)
        print(f"swing step {step:02d}: t={t:5.2f} angle={angle:6.3f} pos=({x:6.2f}, {z:6.2f})")

    # 2. Jump test: simulate 30 frames of jump from vine 0
    t = 0.75  # near peak swing
    omega = (2.0 * math.pi) / 3.0
    angle = 0.785398 * math.sin(omega * t)
    angVel = 0.785398 * omega * math.cos(omega * t)
    length = 0.5 * kVineHeight
    x = length * math.sin(angle)
    z = kVineTopZ - length * math.cos(angle)
    tangSpeed = length * angVel
    velX = tangSpeed * math.cos(angle) + 12.0
    velZ = -tangSpeed * math.sin(angle) + 6.0
    if velX < 4.0: velX = 4.0

    dt = 0.033
    caught = False
    for frame in range(40):
        velZ += kGravity * dt
        x += velX * dt
        z += velZ * dt
        # Check catch vine 1 (at X = 30.0)
        if abs(x - 30.0) < 4.0 and 4.0 <= z <= 28.0 and not caught:
            caught = True
        print(f"jump frame {frame:02d}: pos=({x:6.2f}, {z:6.2f}) vel=({velX:6.2f}, {velZ:6.2f}) caught={int(caught)}")

    # 3. Score calculation tests
    for treas, time_left, reached in [
        (0, 0.0, False), (5, 0.0, False), (10, 0.0, False),
        (5, 10.0, True), (10, 25.0, True), (15, 45.0, True)
    ]:
        score = treas
        if reached and time_left > 0.0:
            bonus = int(kBaseBonus + kBonusPerSecond * time_left)
            if bonus < 0: bonus = 0
            score += bonus
        score = max(1, score)
        print(f"score: treas={treas:2d} time={time_left:4.1f} end={int(reached)} -> {score:2d}")

if __name__ == '__main__':
    main()
