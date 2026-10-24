#!/usr/bin/env python3
import math

ScoreToJellyBeansMultiplier = 5
BaseBonusOnCompletion = 15
BonusPerSecondLeft = 0.8

def calculateJellybeans(rawScore, timeLeft, completed):
    total = rawScore
    if completed and timeLeft > 0:
        total += BaseBonusOnCompletion + int(BonusPerSecondLeft * timeLeft)
    if total < 0:
        total = 0
    beans = math.sqrt(total * ScoreToJellyBeansMultiplier)
    return max(1, int(beans + 0.5))

def main():
    cases = [
        (0, 0, False),
        (5, 0, False),
        (10, 50.0, True),
        (25, 80.0, True),
        (0, 100.0, True),
        (-5, 0, False),
        (15, 30.5, True)
    ]
    for raw, tl, comp in cases:
        jb = calculateJellybeans(raw, tl, comp)
        print(f"raw={raw} tl={tl:.1f} comp={comp} -> {jb}")

if __name__ == '__main__':
    main()
