#!/usr/bin/env python3
"""Run existing host regressions with isolated Python and worktree-local scratch."""

import json
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "sdmc-out/qafix/regressions"
OUT.mkdir(parents=True, exist_ok=True)
(OUT / "tmp").mkdir(exist_ok=True)
wrapper = OUT / "python"
wrapper.write_text(
    '#!/bin/sh\nexec "' + str(ROOT / ".venv/bin/python") + '" -I -B "$@"\n'
)
wrapper.chmod(0o755)
env = dict(
    os.environ,
    PY=str(wrapper),
    TMPDIR=str(OUT / "tmp"),
    OPENBLAS_NUM_THREADS="1",
    OMP_NUM_THREADS="1",
    TT_ROOT=str(ROOT),
    TT_RESOURCES=str(ROOT / "ref/ref/resources"),
    TT_RES=str(ROOT / "ref/ref/resources"),
    TT_UPSTREAM=str(ROOT / "ref/ref/open-toontown"),
    TT_SERVER_PY=str(ROOT / "tools/server/.venv/bin/python"),
)
commands = [
    (
        f"{name}_test",
        ["bash", f"tools/{name}_test/run_tests.sh", "ref/ref/open-toontown"],
    )
    for name in (
        "cannon",
        "catch",
        "cog_thief",
        "diving",
        "golf",
        "ice",
        "maze",
        "pairing",
        "pattern",
        "photo",
        "race",
        "ring",
        "tag",
        "target",
        "tug",
        "two_d",
        "vine",
    )
]
commands += [
    (name, ["bash", path])
    for name, path in (
        ("battle_fx", "tools/battle_fx_test/run_test.sh"),
        ("battle_cam", "tools/battle_cam_test/run_test.sh"),
        ("book", "tools/book_test/run.sh"),
        ("fishing", "tools/fishing_test/run.sh"),
        ("level_games", "tools/level_games_test/run_test.sh"),
    )
]
commands += [
    (name, [str(ROOT / ".venv/bin/python"), "-I", "-B", path, *args])
    for name, path, args in (
        ("anim", "tools/anim_test/run.py", []),
        ("factory", "tools/factory_test/run_test.py", []),
        ("estate", "tests/test_estates.py", []),
        ("racing", "tests/racing_contract_test.py", []),
        ("facility", "tests/facility_runtime_test.py", []),
        ("toon_dna", "tools/toon_dna_test/run_test.py", []),
        ("zone", "tools/zone_test/run.py", ["--unit"]),
        (
            "shop",
            "tools/shop_test/run_test.py",
            ["--upstream", "ref/ref/open-toontown"],
        ),
        ("cogbldg", "tools/cogbldg_test/run_test.py", []),
        ("tailor", "tools/tailor_test/run_test.py", []),
        ("cchar", "tools/cchar_test/run.py", []),
    )
]
commands += [
    (name, ["bash", path])
    for name, path in (
        ("nettest", "tools/nettest/run_tests.sh"),
        ("name", "tools/name_test/run_test.sh"),
        ("suit", "tools/suit_test/run.sh"),
        ("collision", "tools/collision_test/run.sh"),
        ("transition", "tools/transition_test/run_tests.sh"),
        ("boss", "tools/boss_test/run.sh"),
        ("cogdo", "tools/cogdo_test/run.sh"),
    )
]
commands += [("clerks", ["bash", "tools/clerks_test/run.sh"]),
             ("interiorlife", ["bash", "tools/interiorlife_test/run_tests.sh"])]
selected = set(sys.argv[1:])
results = (
    json.loads((OUT / "results.json").read_text())
    if selected and (OUT / "results.json").exists()
    else {}
)
for name, command in commands:
    if selected and name not in selected:
        continue
    with (OUT / (name + ".log")).open("w") as log:
        result = subprocess.run(
            command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT
        )
    results[name] = result.returncode
    print(
        name,
        "PASS"
        if result.returncode == 0
        else "UNAVAILABLE"
        if result.returncode == 77
        else f"FAIL ({result.returncode})",
        flush=True,
    )
(OUT / "results.json").write_text(json.dumps(results, indent=2) + "\n")
sys.exit(any(results.values()))
