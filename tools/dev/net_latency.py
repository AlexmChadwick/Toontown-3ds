#!/usr/bin/env python3
"""Bot -> 3DS chat latency check (the cross-play regression gate).

    tools/dev/net_latency.py STAMPS LOG [--max-median-ms 45] [--min-samples 5]

STAMPS is the file written by `smoke_test.py --sc IDX --stamp STAMPS` (one
'<epoch ms> sc <IDX>' per send). LOG is the 3DS log.txt ('[<epoch ms>] net: chat
rx <doId> sc <IDX>', osGetTime). Both clocks are the host's when run in Azahar, so
rx_time - tx_time is the end-to-end latency bot -> 3DS net thread -> main-loop
drain. Each send is matched to the first unmatched rx of the same index at or
after it. Exit 0 = median under the limit with enough samples.
"""
import argparse
import re
import statistics
import sys

RX = re.compile(r"^\[(\d+)\] net: chat rx \d+ sc (\d+)")


def load_stamps(path):
    out = []
    for line in open(path):
        p = line.split()
        if len(p) == 3 and p[1] == "sc":
            out.append((int(p[0]), int(p[2])))
    return out


def load_rx(path):
    out = []
    for line in open(path, errors="replace"):
        m = RX.match(line)
        if m:
            out.append((int(m.group(1)), int(m.group(2))))
    return out


def match(stamps, rx):
    lat, used, j = [], set(), 0
    for ts, idx in stamps:
        for k in range(len(rx)):
            if k in used:
                continue
            t, i = rx[k]
            if i == idx and t >= ts - 2:   # -2: ms rounding between the two clocks
                used.add(k)
                lat.append(max(0, t - ts))
                break
    return lat


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stamps")
    ap.add_argument("log")
    ap.add_argument("--max-median-ms", type=float, default=45.0)
    ap.add_argument("--min-samples", type=int, default=5)
    a = ap.parse_args()
    stamps, rx = load_stamps(a.stamps), load_rx(a.log)
    lat = match(stamps, rx)
    print("sent %d, received %d, matched %d" % (len(stamps), len(rx), len(lat)))
    if len(lat) < a.min_samples:
        print("FAIL: need >= %d matched samples" % a.min_samples)
        return 1
    lat.sort()
    med = statistics.median(lat)
    p90 = lat[min(len(lat) - 1, int(len(lat) * 0.9))]
    print("latency ms: min %d median %.1f p90 %d max %d (limit median %.0f)" % (
        lat[0], med, p90, lat[-1], a.max_median_ms))
    if med > a.max_median_ms:
        print("FAIL: median over limit")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
