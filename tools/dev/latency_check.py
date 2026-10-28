#!/usr/bin/env python3
"""Receive-latency check for the 3DS client (cross-play regression).

    python3 -I tools/dev/latency_check.py <3ds log.txt> <bot output> [--max-one-way-ms 45]

Setup (tools/dev/latency_run.sh does all of it): the 3DS runs with the autotest trigger line
`chat every 2`, so it says SpeedChat 100 every 2 s (`net: tx chat sc 100`). The PC bot runs
`tools/server/smoke_test.py --echo 100 101`: as soon as it hears 100 it says 101 and prints both
wall-clock times. The 3DS logs `net: rx chat sc 101 ... queued N ms` when its main loop handles
the reply.

The two clocks cannot be compared directly: Azahar's 3DS clock is the host's *local* time with
an RTC of one-second resolution, so a cross-clock one-way figure is off by up to a second. What
is measured instead:

  * round trip, on the 3DS clock alone: tx 100 -> handled rx 101 (exact);
  * bot turnaround (exact, host clock), subtracted from the round trip;
  * one way (bot -> 3DS handled) = (round trip - turnaround) / 2, the NTP estimate (assumes a
    symmetric path; the 3DS send side is the faster one, so this *under*-states receive latency
    by at most half the asymmetry);
  * queued: socket arrival on the 3DS net thread -> handled by the main loop (exact; the part
    the client's frame loop adds);
  * the 3DS's own 10 s `net: rx latency` summaries over every message.

Exits 1 if the one-way median exceeds --max-one-way-ms, or nothing could be paired.
"""
import argparse
import re
import statistics
import sys

TX = re.compile(r"^\[(\d+)\] net: tx chat sc (\d+)")
RX = re.compile(r"^\[(\d+)\] net: rx chat sc (\d+) from (\d+) queued (\d+) ms")
SUMMARY = re.compile(r"net: rx latency (\d+) msgs, median (\d+) ms, p90 (\d+) ms, max (\d+) ms")
ECHO = re.compile(r"^echo rx sc (\d+) from (\d+) at ([0-9.]+) tx sc (\d+) at ([0-9.]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("bot")
    ap.add_argument("--probe", type=int, default=100, help="SpeedChat id the 3DS says")
    ap.add_argument("--reply", type=int, default=101, help="SpeedChat id the bot echoes")
    ap.add_argument("--max-one-way-ms", type=float, default=45.0)
    a = ap.parse_args()

    tx, rx, summaries = [], [], []
    with open(a.log, errors="replace") as f:
        for line in f:
            m = TX.match(line)
            if m and int(m.group(2)) == a.probe:
                tx.append(int(m.group(1)))
            m = RX.match(line)
            if m and int(m.group(2)) == a.reply:
                rx.append((int(m.group(1)), int(m.group(4))))
            m = SUMMARY.search(line)
            if m:
                summaries.append(tuple(int(g) for g in m.groups()))
    turnaround = []
    with open(a.bot, errors="replace") as f:
        for line in f:
            m = ECHO.match(line.strip())
            if m and int(m.group(1)) == a.probe:
                turnaround.append((float(m.group(5)) - float(m.group(3))) * 1000.0)

    rtt, queued = [], []
    j = 0
    for t1 in tx:
        while j < len(rx) and rx[j][0] < t1:
            j += 1
        if j < len(rx) and rx[j][0] - t1 < 1500:
            rtt.append(rx[j][0] - t1)
            queued.append(rx[j][1])
            j += 1

    print("probes sent %d, echoes heard by the bot %d, replies handled by the 3DS %d" %
          (len(tx), len(turnaround), len(rtt)))
    if not rtt:
        print("FAIL: nothing to measure")
        return 1
    turn = statistics.median(turnaround) if turnaround else 0.0
    rtt_med = statistics.median(rtt)
    one_way = [(r - turn) / 2.0 for r in rtt]
    ow_med = statistics.median(one_way)
    print("round trip (3DS clock): median %.0f ms, min %d, max %d" % (rtt_med, min(rtt), max(rtt)))
    print("bot turnaround: median %.1f ms" % turn)
    print("one way, bot -> 3DS handled (NTP estimate): median %.0f ms, max %.0f" % (ow_med, max(one_way)))
    print("queued, 3DS socket -> handled: median %.0f ms, max %d" % (statistics.median(queued), max(queued)))
    for n, m, p90, mx in summaries[-3:]:
        print("3DS rx summary: %d msgs, median %d ms, p90 %d ms, max %d ms" % (n, m, p90, mx))
    if ow_med > a.max_one_way_ms:
        print("FAIL: one-way median %.0f ms > %.0f ms" % (ow_med, a.max_one_way_ms))
        return 1
    print("PASS (one-way median <= %.0f ms)" % a.max_one_way_ms)
    return 0


if __name__ == "__main__":
    sys.exit(main())
