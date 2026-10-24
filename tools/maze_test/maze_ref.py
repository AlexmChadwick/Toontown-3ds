#!/usr/bin/env python3
"""Reference generator for Maze logic host test using upstream Python."""
import os
import sys

def main():
    ott = sys.argv[1] if len(sys.argv) > 1 else "/Users/alexchadwick/toontown-3ds/ref/open-toontown"
    sys.path.insert(0, ott)

    from toontown.minigame import MazeData
    m = MazeData.mazeData['phase_4/models/minigames/maze_1player']
    ct = m['collisionTable']
    width = m['width']
    height = m['height']
    originTX = m['originX']
    originTY = m['originY']
    cellWidth = 2.0
    WALL_OFFSET = 1.0

    def world2tile(x, y):
        import math
        return [int(math.floor(x / cellWidth + originTX)), int(math.floor(y / cellWidth + originTY))]

    def calcFlushCoord(curTile, newTile, centerTile):
        EPSILON = 0.01
        if newTile > curTile:
            return (newTile - centerTile) * cellWidth - EPSILON - WALL_OFFSET
        else:
            return (curTile - centerTile) * cellWidth + WALL_OFFSET

    def doOrthoCollisions(oldX, oldY, newX, newY):
        offsetX = newX - oldX
        offsetY = newY - oldY
        curX = oldX
        curY = oldY
        curTX, curTY = world2tile(curX, curY)

        wallOffX = WALL_OFFSET if offsetX >= 0 else -WALL_OFFSET
        wallOffY = WALL_OFFSET if offsetY >= 0 else -WALL_OFFSET

        testX = curX + offsetX + wallOffX
        testY = curY
        nTX, nTY = world2tile(testX, testY)
        if nTX != curTX and 0 <= nTY < height and 0 <= nTX < width:
            if ct[nTY][nTX] == 1:
                offsetX = calcFlushCoord(curTX, nTX, originTX) - curX

        testX = curX
        testY = curY + offsetY + wallOffY
        nTX, nTY = world2tile(testX, testY)
        if nTY != curTY and 0 <= nTY < height and 0 <= nTX < width:
            if ct[nTY][nTX] == 1:
                offsetY = calcFlushCoord(curTY, nTY, originTY) - curY

        testX = curX + offsetX + wallOffX
        testY = curY + offsetY + wallOffY
        nTX, nTY = world2tile(testX, testY)
        if 0 <= nTY < height and 0 <= nTX < width and ct[nTY][nTX] == 1:
            cX = calcFlushCoord(curTX, nTX, originTX)
            cY = calcFlushCoord(curTY, nTY, originTY)
            if abs(cX - curX) < abs(cY - curY):
                offsetX = cX - curX
            else:
                offsetY = cY - curY

        return curX + offsetX, curY + offsetY

    # Deterministic pseudo-random movement steps
    x, y = 0.0, 0.0
    seed = 12345
    def rng():
        nonlocal seed
        seed = (seed * 1103515245 + 12345) & 0x7fffffff
        return seed

    for step in range(100):
        dx = ((rng() % 200) - 100) / 50.0  # -2.0 to 2.0
        dy = ((rng() % 200) - 100) / 50.0
        nx = x + dx
        ny = y + dy
        outX, outY = doOrthoCollisions(x, y, nx, ny)
        print(f"collide {step:02d}: old=({x:6.2f}, {y:6.2f}) new=({nx:6.2f}, {ny:6.2f}) -> out=({outX:6.2f}, {outY:6.2f})")
        x, y = outX, outY

    # Tile conversion tests
    test_points = [
        (-24.0, -18.0), (-20.0, -16.0), (-14.0, 0.0), (0.0, 0.0), (14.0, 10.0),
        (20.0, 16.0), (24.0, 18.0), (-1.5, 2.5), (3.7, -4.2), (12.1, -17.9)
    ]
    for pt in test_points:
        tx, ty = world2tile(pt[0], pt[1])
        print(f"tile: ({pt[0]:6.2f}, {pt[1]:6.2f}) -> ({tx:2d}, {ty:2d})")

    # Scoring tests
    for taken, all_taken in [(0, 0), (5, 0), (11, 0), (12, 0), (23, 0), (24, 0), (100, 0), (197, 0), (197, 1)]:
        score = max(1, taken // 12) + (8 if all_taken else 0)
        print(f"score: taken={taken:3d} all={all_taken} -> {score:2d}")

if __name__ == '__main__':
    main()
