#!/bin/bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OTT="${1:-$ROOT/ref/open-toontown}"
if [ ! -d "$OTT/toontown" ]; then
    if [ -d "$ROOT/ref/ref/open-toontown/toontown" ]; then
        OTT="$ROOT/ref/ref/open-toontown"
    else
        OTT="/Users/alexchadwick/toontown-3ds/ref/open-toontown"
    fi
fi
PY="${PY:-$ROOT/.venv/bin/python}"
OUT="$HERE/out"
mkdir -p "$OUT"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/maze_logic.cpp" "$HERE/maze_test.cpp" -o "$OUT/maze_test"
"$OUT/maze_test" > "$OUT/cpp.txt"
"$PY" -I "$HERE/maze_ref.py" "$OTT" > "$OUT/py.txt"
if diff -u "$OUT/py.txt" "$OUT/cpp.txt" > "$OUT/diff.txt"; then
    echo "maze_logic matches upstream: $(grep -c '^collide ' "$OUT/py.txt") collides, $(grep -c '^tile: ' "$OUT/py.txt") tiles, $(grep -c '^score: ' "$OUT/py.txt") scores"
else
    head -40 "$OUT/diff.txt"
    echo "MAZE TEST FAILED"
    exit 1
fi
# Stage 2: suits vs the stock modules (a Python with panda3d.otp, e.g. tools/server/.venv).
PY_OTP="${PY_OTP:-$ROOT/tools/server/.venv/bin/python}"
"${CXX:-clang++}" -std=gnu++17 -fno-rtti -fno-exceptions -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -I "$ROOT/source" \
    "$ROOT/source/game/maze_logic.cpp" "$HERE/suits_test.cpp" -o "$OUT/suits_test"
"$OUT/suits_test" | sed 's/-0\.000/0.000/g' > "$OUT/suits_cpp.txt"
"$PY_OTP" "$HERE/suits_ref.py" "$OTT" 2>/dev/null | grep '^[hs]' | sed 's/-0\.000/0.000/g' > "$OUT/suits_py.txt"
# Positions may differ in the last printed digit (float vs double).
if python3 - "$OUT/suits_py.txt" "$OUT/suits_cpp.txt" <<'PYEOF'
import sys
a, b = (open(f).read().split('\n') for f in sys.argv[1:3])
assert len(a) == len(b) and len(a) > 10, (len(a), len(b))
for x, y in zip(a, b):
    xs, ys = x.replace(',', ' ').split(), y.replace(',', ' ').split()
    assert len(xs) == len(ys), (x, y)
    for u, v in zip(xs, ys):
        if u != v:
            assert abs(float(u) - float(v)) <= 0.0015, (x, y)
PYEOF
then echo "maze suits match upstream: $(wc -l < "$OUT/suits_py.txt") lines"; else echo "SUITS TEST FAILED"; exit 1; fi
