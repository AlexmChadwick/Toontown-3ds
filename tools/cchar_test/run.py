#!/usr/bin/env python3
"""Host conformance tests for classic characters (chatter, walk timing, and Dale orbit).

Run with: .venv/bin/python -I tools/cchar_test/run.py
"""
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def wrap180(deg: float) -> float:
    while deg > 180.0:
        deg -= 360.0
    while deg < -180.0:
        deg += 360.0
    return deg


def py_build_segments(pts, speed: float, initial_heading: float = 0.0):
    if len(pts) < 2 or speed <= 0:
        return []
    heading = initial_heading
    origin = pts[0]
    segments = []
    deg_to_rad = 3.141592653589793 / 180.0
    for k in range(len(pts) - 1):
        p0 = pts[k]
        p1 = pts[k + 1]
        dx = p1[0] - p0[0]
        dy = p1[1] - p0[1]
        dz = p1[2] - p0[2]
        dist = math.sqrt(dx * dx + dy * dy + dz * dz)
        duration = dist / speed
        cur = wrap180(heading)
        dest = wrap180(math.atan2(-(p1[0] - origin[0]), p1[1] - origin[1]) / deg_to_rad)
        while dest - cur > 180.0:
            dest -= 360.0
        while dest - cur < -180.0:
            dest += 360.0
        h0 = cur
        h1 = dest
        turn_time = abs(dest) / 270.0
        if duration - turn_time > 0.01:
            move_time = duration - turn_time
            length = max(turn_time, move_time)
        else:
            move_time = 0.0
            turn_time = 0.0
            h1 = cur
            length = 0.0
        heading = dest
        segments.append({
            'turnTime': turn_time,
            'moveTime': move_time,
            'length': length,
            'h0': h0,
            'h1': h1,
        })
    return segments


def py_compute_dale_orbit(t: float, duration: float, chip_src, chip_dst,
                          src_theta: float, dest_theta: float, revs: float, radius: float):
    deg_to_rad = 3.141592653589793 / 180.0
    if duration > 0.001 and t < duration:
        frac = max(0.0, min(1.0, t / duration))
        cur_x = chip_src[0] + (chip_dst[0] - chip_src[0]) * frac
        cur_y = chip_src[1] + (chip_dst[1] - chip_src[1]) * frac
        cur_z = chip_src[2] + (chip_dst[2] - chip_src[2]) * frac
        cur_theta = src_theta + frac * revs * (2.0 * math.pi)
        x = cur_x + math.cos(cur_theta) * radius
        y = cur_y + math.sin(cur_theta) * radius
        z = cur_z
        deg = (cur_theta / deg_to_rad) % 360.0
        if deg < 0:
            deg += 360.0
        h = deg
    else:
        x = chip_dst[0] + math.cos(dest_theta) * radius
        y = chip_dst[1] + math.sin(dest_theta) * radius
        z = chip_dst[2]
        deg = (dest_theta / deg_to_rad) % 360.0
        if deg < 0:
            deg += 360.0
        h = deg
    return x, y, z, h


def find_paths_dir() -> Path:
    candidates = [
        ROOT / 'sdmc-out/3ds/toontown3ds/data/chars',
        ROOT.parent / 'toontown-3ds/sdmc-out/3ds/toontown3ds/data/chars',
    ]
    for c in candidates:
        if c.exists() and any(c.glob('*.paths')):
            return c
    raise FileNotFoundError("Could not find directory with *.paths files")


def main():
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = Path(tmp_dir)
        binary = tmp_path / 'cchar_test'

        # 1. Compile host test binary
        compile_cmd = [
            'c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-fno-rtti', '-fno-exceptions',
            '-fsanitize=address,undefined', '-g',
            '-I', str(ROOT / 'source'),
            str(ROOT / 'tools/cchar_test/cchar_test.cpp'),
            str(ROOT / 'source/ui/cchar_chatter_data.cpp'),
            str(ROOT / 'source/world/cchar.cpp'),
            '-o', str(binary),
        ]
        print(f"Compiling: {' '.join(compile_cmd)}")
        subprocess.run(compile_cmd, check=True)

        # 2. Run built-in C++ unit tests
        print("Running built-in C++ unit tests...")
        subprocess.run([str(binary)], check=True)

        # 3. Test segment timing against real .paths JSON files
        paths_dir = find_paths_dir()
        test_path_files = ['mickey_2000.paths', 'chip_6000.paths', 'daisy_2000.paths', 'goofy_5000.paths']
        checked_edges = 0

        for pf_name in test_path_files:
            pf = paths_dir / pf_name
            if not pf.exists():
                continue
            with open(pf, 'r', encoding='utf-8') as f:
                data = json.load(f)
            speed = float(data.get('speed', 5.0))
            for edge in data.get('edges', []):
                pts = edge.get('points', [])
                if len(pts) < 2 or len(pts) > 60:
                    continue

                py_segs = py_build_segments(pts, speed, initial_heading=0.0)

                # Feed into C++ binary
                input_str = f"{speed} 0.0 {len(pts)}\n"
                for p in pts:
                    input_str += f"{p[0]} {p[1]} {p[2]}\n"

                res = subprocess.run([str(binary), '--cli-timing'],
                                     input=input_str,
                                     capture_output=True,
                                     text=True,
                                     check=True)
                lines = res.stdout.strip().splitlines()
                n_cpp = int(lines[0])
                assert n_cpp == len(py_segs), f"Mismatch in seg count: cpp={n_cpp}, py={len(py_segs)}"

                for idx, line in enumerate(lines[1:]):
                    t_time, m_time, length, h0, h1 = [float(x) for x in line.split()]
                    py_s = py_segs[idx]
                    assert abs(t_time - py_s['turnTime']) < 1e-3, f"turnTime mismatch at edge {edge['src']}->{edge['dst']}"
                    assert abs(m_time - py_s['moveTime']) < 1e-3, f"moveTime mismatch at edge {edge['src']}->{edge['dst']}"
                    assert abs(length - py_s['length']) < 1e-3, f"length mismatch at edge {edge['src']}->{edge['dst']}"
                    assert abs(h0 - py_s['h0']) < 1e-3, f"h0 mismatch at edge {edge['src']}->{edge['dst']}"
                    assert abs(h1 - py_s['h1']) < 1e-3, f"h1 mismatch at edge {edge['src']}->{edge['dst']}"

                checked_edges += 1

        print(f"Validated {checked_edges} edges from .paths against CharWalkState interval timing.")

        # 4. Test Dale orbit for recorded leg a -> b
        dale_pf = paths_dir / 'dale_6000.paths'
        assert dale_pf.exists(), "dale_6000.paths must exist"
        with open(dale_pf, 'r', encoding='utf-8') as f:
            dale_data = json.load(f)
        edge_ab = next(e for e in dale_data['edges'] if e['src'] == 'a' and e['dst'] == 'b')
        pts = edge_ab['points']
        chip_dist = edge_ab['distance'] # 80.1581

        s_off = (3.0, 0.0)
        d_off = (-1.5, 2.0)
        src_theta = math.atan2(s_off[1], s_off[0])
        if src_theta < 0:
            src_theta += 2 * math.pi
        src_rev = (2 * math.pi - src_theta) / (2 * math.pi) if src_theta > 0 else 0.0

        dest_theta = math.atan2(d_off[1], d_off[0])
        if dest_theta < 0:
            dest_theta += 2 * math.pi

        revs = chip_dist / 13.0 + src_rev
        ending_theta = src_theta + (revs % 1.0) * 2 * math.pi
        diff_theta = dest_theta - ending_theta
        dest_rev = diff_theta / (2 * math.pi)
        revs += dest_rev
        while revs < 1.0:
            revs += 1.0

        chip_src = (pts[0][0] - s_off[0], pts[0][1] - s_off[1], pts[0][2])
        chip_dst = (pts[-1][0] - d_off[0], pts[-1][1] - d_off[1], pts[-1][2])
        radius = 3.0
        chip_speed = 3.0
        duration = chip_dist / chip_speed # 26.719355...

        sample_ts = [0.0, 0.1 * duration, 0.25 * duration, 0.5 * duration, 0.75 * duration, duration]
        for t in sample_ts:
            py_x, py_y, py_z, py_h = py_compute_dale_orbit(t, duration, chip_src, chip_dst,
                                                           src_theta, dest_theta, revs, radius)

            inp = f"{t} {duration} {chip_src[0]} {chip_src[1]} {chip_src[2]} " \
                  f"{chip_dst[0]} {chip_dst[1]} {chip_dst[2]} " \
                  f"{src_theta} {dest_theta} {revs} {radius}\n"

            res = subprocess.run([str(binary), '--cli-dale'],
                                 input=inp,
                                 capture_output=True,
                                 text=True,
                                 check=True)
            cx, cy, cz, ch = [float(v) for v in res.stdout.strip().split()]

            assert abs(cx - py_x) < 1e-2, f"Dale X mismatch at t={t}: cpp={cx}, py={py_x}"
            assert abs(cy - py_y) < 1e-2, f"Dale Y mismatch at t={t}: cpp={cy}, py={py_y}"
            assert abs(cz - py_z) < 1e-2, f"Dale Z mismatch at t={t}: cpp={cz}, py={py_z}"
            # Compare angles with circular difference
            diff_h = abs(ch - py_h)
            if diff_h > 180.0:
                diff_h = abs(diff_h - 360.0)
            assert diff_h < 0.2, f"Dale H mismatch at t={t}: cpp={ch}, py={py_h}"

        print(f"Validated Dale orbit across {len(sample_ts)} time samples for leg a -> b.")
        print("ALL TESTS PASSED SUCCESSFULLY.")


if __name__ == '__main__':
    main()
