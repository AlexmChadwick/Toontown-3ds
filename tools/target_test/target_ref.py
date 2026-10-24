#!/usr/bin/env python3
import sys
import math
import random

ENVIRON_WIDTH = 150.0
ENVIRON_LENGTH = 300.0
fieldWidth = ENVIRON_WIDTH * 3.0
fieldLength = ENVIRON_LENGTH * 3.7

def checkPlace(placeX, placeY, fillSize, placeList):
    for place in placeList:
        distance = math.sqrt((place[0] - placeX) ** 2 + (place[1] - placeY) ** 2)
        if distance - (fillSize + place[2]) <= 0.0:
            return 0
    return 1

def setupTargets(seed):
    random.seed(seed)
    targetList = [8, 4, 2, 0]
    targetValue = [10, 16, 21, 28]
    targetSize = [31.0, 15.0, 7.0, 3.5]
    targetSubParts = [2, 2, 2, 1]
    highestValue = 28
    placeValue = highestValue * 0.5

    targetsPlaced = []
    placeList = []
    for typeIndex in range(len(targetList)):
        for targetIndex in range(targetList[typeIndex]):
            goodPlacement = 0
            while not goodPlacement:
                placeX = random.random() * (fieldWidth * 0.6) - fieldWidth * 0.6 * 0.5
                placeY = (random.random() * 0.6 + (0.0 + 0.4 * (placeValue * 1.0 / (highestValue * 1.0)))) * fieldLength
                fillSize = targetSize[typeIndex]
                goodPlacement = checkPlace(placeX, placeY, fillSize, placeList)
            placeList.append((placeX, placeY, fillSize))
            subIndex = targetSubParts[typeIndex]
            while subIndex:
                combinedIndex = typeIndex + subIndex - 1
                targetsPlaced.append((placeX, placeY, targetSize[combinedIndex], targetValue[combinedIndex]))
                subIndex -= 1
    return targetsPlaced

def evalScore(targets, sx, sy):
    top = 0
    for t in targets:
        dx = t[0] - sx
        dy = t[1] - sy
        if math.sqrt(dx*dx + dy*dy) < t[2] and t[3] > top:
            top = t[3]
    return max(1, top)

def main():
    for seed in [5, 1234, 4000]:
        targets = setupTargets(seed)
        print(f"seed={seed} targets={len(targets)}")
        for i in range(min(5, len(targets))):
            t = targets[i]
            print(f"  t[{i}]: x={t[0]:.2f} y={t[1]:.2f} r={t[2]:.2f} v={t[3]}")
        for (sx, sy) in [(0, 500), (50, 600), (100, 300), (-50, 700)]:
            s = evalScore(targets, sx, sy)
            print(f"  score({sx},{sy})={s}")

if __name__ == '__main__':
    main()
