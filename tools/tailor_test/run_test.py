#!/usr/bin/env python3
"""Compare generated tailor clothing lists and DNA encoding with upstream ToonDNA.py."""

from __future__ import annotations

import ast
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[2]
UPSTREAM = ROOT.parent / "toontown-3ds" / "ref" / "open-toontown" / "toontown" / "toon" / "ToonDNA.py"


def assignments(tree):
    values = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            try:
                values[node.targets[0].id] = eval(compile(ast.Expression(node.value), str(UPSTREAM), "eval"),
                                                  {"__builtins__": {}}, values)
            except (NameError, TypeError, ValueError, SyntaxError):
                pass
    return values


def main():
    with tempfile.TemporaryDirectory() as temp:
        exe = pathlib.Path(temp) / "tailor_test"
        subprocess.run(
            ["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "source"),
             str(ROOT / "tools" / "tailor_test" / "tailor_test.cpp"),
             str(ROOT / "source" / "world" / "toon_dna.cpp"), "-o", str(exe)],
            check=True,
        )
        lines = subprocess.check_output([str(exe)], text=True).splitlines()

    tree = ast.parse(UPSTREAM.read_text())
    values = assignments(tree)
    shirt_styles = values["ShirtStyles"]
    bottom_styles = values["BottomStyles"]
    girl_bottoms = values["GirlBottoms"]
    tailor_collections = values["TailorCollections"]
    make_a_toon = values["MAKE_A_TOON"]
    expected = {
        "shirts": {
            "m": [(shirt_styles[s][0], c[0], shirt_styles[s][1], c[1])
                  for s in tailor_collections[make_a_toon][0] for c in shirt_styles[s][2]],
            "f": [(shirt_styles[s][0], c[0], shirt_styles[s][1], c[1])
                  for s in tailor_collections[make_a_toon][1] for c in shirt_styles[s][2]],
        },
        "bottoms": {
            "m": [(bottom_styles[s][0], c, False) for s in tailor_collections[make_a_toon][2]
                  for c in bottom_styles[s][1]],
            "f": [(bottom_styles[s][0], c, bool(girl_bottoms[bottom_styles[s][0]][1] == 1))
                  for s in tailor_collections[make_a_toon][3]
                  for c in bottom_styles[s][1]],
        },
    }

    pos = 0
    actual = {"shirts": {}, "bottoms": {}}
    for kind, gender in (("shirts", "m"), ("shirts", "f"), ("bottoms", "m"), ("bottoms", "f")):
        name, count = lines[pos].split()
        assert name == kind and int(count) >= 1
        pos += 1
        rows = []
        for _ in range(int(count)):
            row = tuple(map(int, lines[pos].split()))
            rows.append(row[:4] if kind == "shirts" else (row[0], row[1], bool(row[2])))
            pos += 1
        actual[kind][gender] = rows
        assert rows == expected[kind][gender], (kind, gender, rows, expected[kind][gender])

    dna = tuple(map(int, lines[pos].split()[1:]))
    assert dna == (ord("t"), 0, 1, 1, 0, 1, 0, 1, 0, 5, 6, 25, 0, 25, 25), dna
    print("tailor host test: PASS")


if __name__ == "__main__":
    main()
