#!/usr/bin/env python3
"""Reference generator for Match Minnie / Pairing minigame logic host test."""
import random
import sys

def main():
    seeds = [0, 5, 42, 123456, 3999999, 1000000]

    for seed in seeds:
        rng = random.Random(seed)
        deck = list(range(52))
        rng.shuffle(deck)
        filtered = [c for c in deck if (c % 13) <= 7]
        ranks = [c % 13 for c in filtered]
        suits = [c // 13 for c in filtered]
        print(f"seed {seed}: count={len(filtered)}")
        print("  cards:", " ".join(f"{c:02d}" for c in filtered))
        print("  ranks:", " ".join(f"{r:d}" for r in ranks))
        print("  suits:", " ".join(f"{s:d}" for s in suits))

    # Low flip modifier & bonus tests
    # matches from 1 to 16, flips from 2 to 64
    for matches in [1, 4, 8, 12, 16]:
        for flips in [matches * 2, int(matches * 2 * 1.6), matches * 4, matches * 5]:
            idealFlips = round(matches * 2 * 1.6)
            if idealFlips < 2:
                idealFlips = 2
            maxFlipsForBonus = idealFlips * 2
            if flips < idealFlips:
                mod = 1.0
            elif maxFlipsForBonus < flips:
                mod = 0.0
            else:
                div = maxFlipsForBonus - idealFlips
                diff = maxFlipsForBonus - flips
                mod = float(diff) / div
            bonus = int(mod * matches)
            # Final score
            score = max(1, matches) + bonus
            if matches == 16:
                score += round(32 / 4.0)
            print(f"eval: matches={matches:02d} flips={flips:02d} -> bonus={bonus:02d} score={score:02d}")

if __name__ == '__main__':
    main()
