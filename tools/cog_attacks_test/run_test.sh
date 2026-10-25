#!/bin/sh
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${TMPDIR:-/tmp}/tt_cog_attacks_test
mkdir -p "$OUT"

echo "=== Running gen_cog_attacks.py --check ==="
"$ROOT/.venv/bin/python" -I "$ROOT/tools/gen_cog_attacks.py" --check

echo "=== Building cog_attacks_test ==="
clang++ -std=c++17 -O2 -Wall -Wextra -Werror \
    -I "$ROOT/source" \
    "$ROOT/tools/cog_attacks_test/cog_attacks_test.cpp" \
    -o "$OUT/cog_attacks_test"

echo "=== Running cog_attacks_test ==="
"$OUT/cog_attacks_test" "$ROOT/tools/cog_attacks_test/reference_vectors.json"
echo "=== Cog attacks test passed! ==="
