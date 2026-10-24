#!/usr/bin/env python3

class PatternGameRef:
    def __init__(self, av_ids):
        self.av_ids = av_ids
        self.round = 0
        self.scores = {av: 0 for av in av_ids}
        self.perfect = {av: 1 for av in av_ids}
        self.pattern = []

    def set_pattern(self, pattern):
        self.round += 1
        self.pattern = list(pattern)

    def submit_patterns(self, player_patterns, times):
        fastest_time = 999.0
        fastest_av = 0
        for av in self.av_ids:
            p = player_patterns[av]
            t = times[av]
            if p == self.pattern and t < fastest_time:
                fastest_time = t
                fastest_av = av

        if len(self.av_ids) > 1 and fastest_av != 0:
            self.scores[fastest_av] += 2

        for av in self.av_ids:
            if player_patterns[av] == self.pattern:
                self.scores[av] += self.round
            else:
                self.perfect[av] = 0

        if self.round >= 4:
            for av in self.av_ids:
                if self.perfect[av]:
                    self.scores[av] += 4

def main():
    # Test 1: Solo perfect game
    game1 = PatternGameRef([1001])
    patterns = [
        [0, 1],
        [0, 1, 3, 2],
        [0, 1, 3, 2, 2, 0],
        [0, 1, 3, 2, 2, 0, 1, 3]
    ]
    for p in patterns:
        game1.set_pattern(p)
        game1.submit_patterns({1001: list(p)}, {1001: 2.5})
    print(f"solo perfect: score={game1.scores[1001]} perfect={game1.perfect[1001]}")

    # Test 2: Solo imperfect game (missed round 3)
    game2 = PatternGameRef([1001])
    for i, p in enumerate(patterns):
        game2.set_pattern(p)
        if i == 2:
            input_p = list(p)
            input_p[-1] = (input_p[-1] + 1) % 4
            game2.submit_patterns({1001: input_p}, {1001: 3.0})
        else:
            game2.submit_patterns({1001: list(p)}, {1001: 2.0})
    print(f"solo imperfect: score={game2.scores[1001]} perfect={game2.perfect[1001]}")

    # Test 3: Multiplayer 2 players (av 1001 faster in round 1 and 2, av 1002 faster in round 3)
    game3 = PatternGameRef([1001, 1002])
    for i, p in enumerate(patterns):
        game3.set_pattern(p)
        t1 = 2.0 if i < 2 else 3.5
        t2 = 3.0 if i < 2 else 2.5
        game3.submit_patterns({1001: list(p), 1002: list(p)}, {1001: t1, 1002: t2})
    print(f"multiplayer: p1={game3.scores[1001]} p2={game3.scores[1002]}")

if __name__ == '__main__':
    main()
