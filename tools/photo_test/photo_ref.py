#!/usr/bin/env python3

class PhotoGameRef:
    def __init__(self, av_ids, film_count=32):
        self.av_ids = av_ids
        self.film_count = film_count
        self.film_used = {av: 0 for av in av_ids}
        self.assignments = [] # [(subject, pose, [scores], highScorer)]
        for i in range(5):
            self.assignments.append([i, 0, [0.0]*len(av_ids), None])

    def set_assignment(self, slot, subject, pose):
        self.assignments[slot][0] = subject
        self.assignments[slot][1] = pose

    def submit_photo(self, av, subject, pose, stars):
        p_idx = self.av_ids.index(av)
        self.film_used[av] += 1
        for a in self.assignments:
            if a[0] == subject and a[1] == pose:
                if stars > a[2][p_idx]:
                    a[2][p_idx] = stars
                    high = a[3]
                    if high is None or a[2][p_idx] > a[2][high]:
                        a[3] = p_idx

    def calculate_scores(self):
        player_bonus = [0.0] * len(self.av_ids)
        team_score = 0.0
        for a in self.assignments:
            high = a[3]
            if high is not None:
                team_score += a[2][high]
                player_bonus[high] += 1.0

        scores = {}
        for av in self.av_ids:
            p_idx = self.av_ids.index(av)
            scores[av] = round(team_score + player_bonus[p_idx])
        return scores

def main():
    # Test 1: Solo game, 3 photos matching assignments
    g1 = PhotoGameRef([1001], film_count=3)
    g1.submit_photo(1001, 0, 0, 4.0)
    g1.submit_photo(1001, 1, 0, 5.0)
    g1.submit_photo(1001, 2, 0, 3.0)
    s1 = g1.calculate_scores()
    print(f"solo: score={s1[1001]}")

    # Test 2: Multiplayer 2 players competing on 5 assignments
    g2 = PhotoGameRef([1001, 1002], film_count=5)
    # p1 snaps assignments 0 (5.0), 1 (3.0), 2 (4.0)
    g2.submit_photo(1001, 0, 0, 5.0)
    g2.submit_photo(1001, 1, 0, 3.0)
    g2.submit_photo(1001, 2, 0, 4.0)
    # p2 snaps assignments 1 (4.0 - beats p1!), 3 (5.0), 4 (2.0)
    g2.submit_photo(1002, 1, 0, 4.0)
    g2.submit_photo(1002, 3, 0, 5.0)
    g2.submit_photo(1002, 4, 0, 2.0)
    s2 = g2.calculate_scores()
    print(f"multi: p1={s2[1001]} p2={s2[1002]}")

if __name__ == '__main__':
    main()
