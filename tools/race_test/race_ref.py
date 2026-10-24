#!/usr/bin/env python3
import math

NumberToWin = 14
ChanceRewards = (
    ((1, 0), "ForwardOneSpace", 0),
    ((1, 0), "ForwardOneSpace", 0),
    ((1, 0), "ForwardOneSpace", 0),
    ((2, 0), "ForwardTwoSpaces", 0),
    ((2, 0), "ForwardTwoSpaces", 0),
    ((2, 0), "ForwardTwoSpaces", 0),
    ((3, 0), "ForwardThreeSpaces", 0),
    ((3, 0), "ForwardThreeSpaces", 0),
    ((3, 0), "ForwardThreeSpaces", 0),
    ((0, -3), "OthersBackThree", 0),
    ((0, -3), "OthersBackThree", 0),
    ((-1, 0), "BackOneSpace", 0),
    ((-1, 0), "BackOneSpace", 0),
    ((-2, 0), "BackTwoSpaces", 0),
    ((-2, 0), "BackTwoSpaces", 0),
    ((-3, 0), "BackThreeSpaces", 0),
    ((-3, 0), "BackThreeSpaces", 0),
    ((0, 3), "OthersForwardThree", 0),
    ((0, 3), "OthersForwardThree", 0),
    ((0, 0), "Jellybeans2", 2),
    ((0, 0), "Jellybeans2", 2),
    ((0, 0), "Jellybeans2", 2),
    ((0, 0), "Jellybeans2", 2),
    ((0, 0), "Jellybeans4", 4),
    ((0, 0), "Jellybeans4", 4),
    ((0, 0), "Jellybeans4", 4),
    ((0, 0), "Jellybeans4", 4),
    ((0, 0), "Jellybeans10", 10),
    ((0, 0), "None", 0),
    ((NumberToWin, 0), "InstantWinner", 0)
)

class RaceGameRef:
    def __init__(self, av_ids):
        self.av_ids = av_ids
        self.positions = {av: 0 for av in av_ids}
        self.chance_pos = {av: -1 for av in av_ids}
        self.chance_reward = {av: -1 for av in av_ids}
        self.scores = {av: 0 for av in av_ids}

    def set_chance(self, av, pos, reward_idx):
        self.chance_pos[av] = pos
        self.chance_reward[av] = reward_idx

    def process_turn(self, choices):
        # choices is dict {av: val}
        for av in self.av_ids:
            c = choices[av]
            if c > 0:
                freq = list(choices.values()).count(c)
                if freq == 1:
                    self.positions[av] += c
                    if self.positions[av] < 0:
                        self.positions[av] = 0
                    if self.positions[av] == self.chance_pos[av] and self.chance_pos[av] != -1:
                        ridx = self.chance_reward[av]
                        self.chance_pos[av] = -1
                        effect, name, beans = ChanceRewards[ridx]
                        self.scores[av] += beans
                        self.positions[av] += effect[0]
                        if self.positions[av] < 0:
                            self.positions[av] = 0
                        for other in self.av_ids:
                            if other != av:
                                self.positions[other] += effect[1]
                                if self.positions[other] < 0:
                                    self.positions[other] = 0

        # Check winners
        won = any(self.positions[av] >= NumberToWin for av in self.av_ids)
        if won:
            num_winners = sum(1 for av in self.av_ids if self.positions[av] >= NumberToWin)
            for av in self.av_ids:
                if self.positions[av] >= NumberToWin:
                    beans = NumberToWin
                    if num_winners > 1:
                        beans -= 3
                else:
                    beans = math.ceil(self.positions[av] * 0.5)
                self.scores[av] += beans
        return won

def main():
    # Scenario 1: Solo run to finish
    g1 = RaceGameRef([1001])
    g1.set_chance(1001, 7, 23) # chance at pos 7 with 4 jellybeans
    choices1 = [4, 3, 4, 3] # 0 -> 4 -> 7 (hits chance +4 beans) -> 11 -> 14 (win!)
    for c in choices1:
        w = g1.process_turn({1001: c})
        print(f"solo turn(c={c}): pos={g1.positions[1001]} won={w}")
    print(f"solo final score: {g1.scores[1001]}")

    # Scenario 2: Two players, conflicting choices cancel out
    g2 = RaceGameRef([1001, 1002])
    # Turn 1: p1 chooses 3, p2 chooses 3 (conflict! neither moves)
    w1 = g2.process_turn({1001: 3, 1002: 3})
    print(f"p2 turn 1 (conflict): p1={g2.positions[1001]} p2={g2.positions[1002]}")
    # Turn 2: p1 chooses 4, p2 chooses 2 (moves)
    w2 = g2.process_turn({1001: 4, 1002: 2})
    print(f"p2 turn 2 (unique): p1={g2.positions[1001]} p2={g2.positions[1002]}")

if __name__ == '__main__':
    main()
