#!/usr/bin/env python3
import math

PointsDeadCenter = {0: 5, 1: 5, 2: 5, 3: 4, 4: 3}
PointsInCorner = 1
FarthestLength = math.sqrt((20.0 - 1.5)**2 + (15.0 - 1.5)**2)
BonusPointsForPlace = (3, 2, 1, 0)

def calculate_points(length, num_players, place):
    pts = length / FarthestLength * (PointsInCorner - PointsDeadCenter[num_players]) + PointsDeadCenter[num_players]
    bonus_index = place + (4 - num_players)
    points_to_add = int(pts + 0.5) + BonusPointsForPlace[bonus_index]
    return points_to_add

def main():
    # Test scoring formula directly
    test_dists = [0.0, 5.0, 10.0, 15.0, 20.0, FarthestLength]
    for num_players in [1, 2, 4]:
        for place in range(num_players):
            for d in test_dists:
                pts = calculate_points(d, num_players, place)
                print(f"score(n={num_players}, pl={place}, d={d:.2f}) = {pts}")

if __name__ == '__main__':
    main()
