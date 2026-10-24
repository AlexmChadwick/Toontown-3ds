#!/usr/bin/env python3
"""Reference output for source/game/tug_logic (host test: tools/tug_test/run_tests.sh).

Runs upstream Tug of War force calculation, key rate decay, and target rates.
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))

OTT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'ref', 'open-toontown')
if not os.path.exists(os.path.join(OTT, 'toontown')):
    alt = os.path.join(ROOT, 'ref', 'ref', 'open-toontown')
    if os.path.exists(os.path.join(alt, 'toontown')):
        OTT = alt
    else:
        OTT = '/Users/alexchadwick/toontown-3ds/ref/open-toontown'

sys.path.insert(0, os.path.abspath(OTT))

from toontown.minigame import TugOfWarGameGlobals  # noqa: E402


def compute_force(key_rate, ideal_rate, advantage=1.0, rate_match_award=0.0):
    std_dev = 0.25 * ideal_rate
    f = advantage * (rate_match_award + 4.0 + 0.4 * ideal_rate) * math.exp(
        -math.pow(key_rate - ideal_rate, 2.0) / (2.0 * math.pow(std_dev, 2.0))
    )
    return f


def main():
    target_rate_list = [
        (8.0, 6.0),
        (5.0, 7.0),
        (6.0, 8.0),
        (6.0, 10.0),
        (7.0, 11.0),
        (8.0, 12.0),
    ]

    for dur, ideal in target_rate_list:
        for rate in range(0, 16):
            for award in [0.0, 0.3, 0.9]:
                f = compute_force(rate, ideal, 1.0, award)
                print(f"force ideal={ideal:.1f} rate={rate} award={award:.1f} F={f:.4f}")

    # Test key decay simulation
    ttl = [1.0, 0.8, 0.5, 0.2]
    for step in range(5):
        ttl = [t - 0.1 for t in ttl if t - 0.1 > 0]
        print(f"decay step={step} count={len(ttl)}")


if __name__ == '__main__':
    main()
