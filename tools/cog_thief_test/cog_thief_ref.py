#!/usr/bin/env python3
import sys

GameTime = 60
MAX_SCORE = 20
MIN_SCORE = 3

def calcScore(t):
    r = MAX_SCORE - MIN_SCORE
    score = r * (float(t) / GameTime) + MIN_SCORE
    return int(score + 0.5)

def evalFinal(t, stolen):
    s = calcScore(t)
    if stolen == 0:
        s += 8
    return s

def main():
    for t in [0.0, 10.0, 25.5, 45.0, 59.9, 60.0]:
        for stolen in [0, 1, 4]:
            res = evalFinal(t, stolen)
            print(f"t={t:.1f} stolen={stolen} -> {res}")

if __name__ == '__main__':
    main()
