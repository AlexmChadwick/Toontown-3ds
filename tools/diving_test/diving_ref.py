#!/usr/bin/env python3
"""Reference generator for Diving minigame logic host test."""

def main():
    dt = 0.033  # ~30 fps
    kMinX = -20.0
    kMaxX = 20.0
    kMinZ = -38.0
    kMaxZ = 36.0
    kSeabedZ = -36.0
    kBoatZ = 32.0
    kBoatRadiusX = 6.0

    # Test swimmer trajectory
    x = 0.0
    z = 34.0
    xVel = 0.0
    zVel = 0.0
    hasTreasure = False
    freezeTimer = 0.0
    recoveries = 0

    # Deterministic input sequence
    seed = 42
    def rng():
        nonlocal seed
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        return seed

    for frame in range(150):
        inputX = ((rng() % 200) - 100) / 100.0
        inputZ = ((rng() % 200) - 100) / 100.0

        if freezeTimer > 0.0:
            freezeTimer -= dt
            if freezeTimer < 0.0: freezeTimer = 0.0
            inputX = 0.0
            inputZ = 0.0

        xVel *= 0.99
        zVel *= 0.99
        xVel += inputX * 35.0 * dt
        zVel += inputZ * 30.0 * dt

        if xVel > 20.0: xVel = 20.0
        elif xVel < -20.0: xVel = -20.0
        if zVel > 10.0: zVel = 10.0
        elif zVel < -10.0: zVel = -10.0

        x += xVel * dt
        z += zVel * dt

        if hasTreasure:
            z -= 0.8 * dt
        else:
            z += 0.8 * dt

        if x < kMinX: x = kMinX; xVel = 0.0
        elif x > kMaxX: x = kMaxX; xVel = 0.0
        if z < kMinZ: z = kMinZ; zVel = 0.0
        elif z > kMaxZ: z = kMaxZ; zVel = 0.0

        # Test pickup at bottom
        if not hasTreasure and z <= kSeabedZ + 1.0 and abs(x) < 5.0 and freezeTimer == 0.0:
            hasTreasure = True

        # Test obstacle hit at frame 80
        if frame == 80:
            hasTreasure = False
            freezeTimer = 3.0
            xVel = 0.0
            zVel = 0.0

        # Test boat recovery
        if hasTreasure and z >= kBoatZ and abs(x) <= kBoatRadiusX and freezeTimer == 0.0:
            hasTreasure = False
            recoveries += 1

        print(f"dive frame {frame:03d}: pos=({x:6.2f}, {z:6.2f}) vel=({xVel:6.2f}, {zVel:6.2f}) treas={int(hasTreasure)} rec={recoveries}")

    # Test scoring
    for rec in [0, 1, 2, 3, 5, 8]:
        score = max(5, rec * 15)
        print(f"score: rec={rec} -> {score}")

if __name__ == '__main__':
    main()
