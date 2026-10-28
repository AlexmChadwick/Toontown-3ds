#!/usr/bin/env python3
"""Bot -> 3DS chat latency check (the cross-play regression gate).

    tools/dev/net_latency.py STAMPS LOG [--max-median-ms 45] [--min-samples 5]

STAMPS is the file written by `smoke_test.py --sc IDX --stamp STAMPS` (one
'<epoch ms> sc <IDX>' per send). LOG is the 3DS log.txt ('[<epoch ms>] net: chat
rx <doId> sc <IDX>', osGetTime). osGetTime counts ms since 1900 and, in Azahar, is NOT
the host's epoch clock: it is ~2.2e12 ms ahead (1900 vs 1970, minus the host's UTC
offset) plus an emulator-start phase of seconds. Pass --offset-ms (guest minus host)
measured for that run; without it a log whose times are not host epoch is rejected
instead of reporting a meaningless latency. With the offset, rx_time - tx_time is the
end-to-end latency bot -> 3DS net thread -> main-loop drain. Each send is matched to the first unmatched rx of the same index at or
after it. With --calibrate RXSTAMPS (bot run with --rxstamp) the offset is derived from the 3DS's own
`net: chat tx sc` lines instead. Exit 0 = median under the limit with enough samples.
"""
import argparse
import re
import statistics
import sys

RX = re.compile(r"^\[(\d+)\] net: chat rx \d+ sc (\d+)")
TX = re.compile(r"^\[(\d+)\] net: chat tx sc (\d+)")
LEG_MS = 3   # measured 3DS -> bot leg in Azahar; the calibrated offset assumes it


def calibrate(rxstamp, log):
    """Guest-minus-host clock offset from chats the 3DS sent and the bot received.

    guest_tx - host_rx = offset - leg, and leg >= 0, so the largest difference over matched
    pairs bounds the offset tightly: offset = max(diff) + LEG_MS. Returns (offset, pairs)."""
    host = []
    for line in open(rxstamp):
        p = line.split()
        if len(p) == 4 and p[1] == "rxsc":
            host.append((int(p[0]), int(p[3])))
    guest = []
    for line in open(log, errors="replace"):
        m = TX.match(line)
        if m:
            guest.append((int(m.group(1)), int(m.group(2))))
    diffs, used = [], set()
    for gt, idx in guest:
        # nearest unmatched host rx of the same index within 60 s (offset is unknown, so match by order)
        for k, (ht, hi) in enumerate(host):
            if k not in used and hi == idx:
                used.add(k)
                diffs.append(gt - ht)
                break
    if not diffs:
        return None, 0
    return max(diffs) + LEG_MS, len(diffs)


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
    ap.add_argument("--offset-ms", type=int, default=0,
                    help="guest osGetTime minus host epoch ms for this run (see the docstring)")
    ap.add_argument("--calibrate", metavar="RXSTAMPS",
                    help="file from smoke_test.py --rxstamp; derives --offset-ms from the 3DS's own chat tx lines "
                         "(the 3DS must send >= 1 chat during the run)")
    a = ap.parse_args()
    if a.calibrate:
        off, n = calibrate(a.calibrate, a.log)
        if off is None:
            print("FAIL: no 'net: chat tx sc' lines in the log matched the bot's rxsc stamps")
            return 2
        print("calibrated offset %d ms from %d tx/rx pairs" % (off, n))
        a.offset_ms = off
    stamps, rx = load_stamps(a.stamps), load_rx(a.log)
    if stamps and rx and not a.offset_ms and abs(rx[0][0] - stamps[0][0]) > 3600 * 1000:
        print("FAIL: log clock is %.1f h away from the stamps; pass --offset-ms (guest - host)" % (
            (rx[0][0] - stamps[0][0]) / 3.6e6))
        return 2
    rx = [(t - a.offset_ms, i) for t, i in rx]
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
