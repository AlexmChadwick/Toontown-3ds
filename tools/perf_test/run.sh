#!/bin/bash
# Commands run from the perf checkout; no emulator or live AI is launched.
set -euo pipefail
OUT=sdmc-out/perf
mkdir -p "$OUT"
CXX="${CXX:-c++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "perf_test: C++ compiler not found: $CXX (set CXX=/path/to/compiler)" >&2
    exit 127
fi
FLAGS=(-std=c++17 -O2 -g -Wall -Wextra -Werror -Isource)
SANITIZERS=(-fsanitize=address,undefined -fno-omit-frame-pointer)
"$CXX" "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/profile.cpp source/core/profile.cpp tools/nettest/log_host.cpp -o "$OUT/profile"
"$OUT/profile" | tee "$OUT/profile.log"
rg -q "test hitch.*hitch total 125" "$OUT/profile.log"
for NAME in hot_paths text nearest; do
    "$CXX" "${FLAGS[@]}" tools/perf_test/$NAME.cpp -o "$OUT/$NAME"
    "$OUT/$NAME"
done
"$CXX" "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/floor_format.cpp source/world/floor_format.cpp -o "$OUT/floor_format"
"$OUT/floor_format"
"$CXX" -Itools/perf_test/stubs "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/text_cache.cpp -o "$OUT/text_cache"
"$OUT/text_cache"
"$CXX" -Itools/perf_test/stubs "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/asset_batch.cpp tools/nettest/log_host.cpp -o "$OUT/asset_batch"
"$OUT/asset_batch"
.venv/bin/python -I -B tools/factory_test/run_test.py > "$OUT/factory.log" 2>&1
"$CXX" "${FLAGS[@]}" tools/perf_test/facility.cpp source/world/level_spec.cpp -o "$OUT/facility"
"$OUT/facility"
"$CXX" "${FLAGS[@]}" source/ui/test/projection_test.cpp -o "$OUT/projection"
"$OUT/projection"
"$CXX" -Itools/perf_test/stubs "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/autotest.cpp source/core/autotest.cpp tools/nettest/log_host.cpp -o "$OUT/autotest"
"$OUT/autotest"
