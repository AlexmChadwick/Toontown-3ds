#!/bin/bash
# Commands run from the perf checkout; no emulator or live AI is launched.
set -euo pipefail
OUT=sdmc-out/perf
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O2 -g -Wall -Wextra -Werror -Isource)
SANITIZERS=(-fsanitize=address,undefined -fno-omit-frame-pointer)
clang++ "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/profile.cpp source/core/profile.cpp tools/nettest/log_host.cpp -o "$OUT/profile"
"$OUT/profile" | tee "$OUT/profile.log"
rg -q "test hitch.*hitch total 125" "$OUT/profile.log"
for NAME in hot_paths text nearest; do
    clang++ "${FLAGS[@]}" tools/perf_test/$NAME.cpp -o "$OUT/$NAME"
    "$OUT/$NAME"
done
clang++ -Itools/perf_test/stubs "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/text_cache.cpp -o "$OUT/text_cache"
"$OUT/text_cache"
clang++ -Itools/perf_test/stubs "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/asset_batch.cpp tools/nettest/log_host.cpp -o "$OUT/asset_batch"
"$OUT/asset_batch"
.venv/bin/python -I -B tools/factory_test/run_test.py > "$OUT/factory.log" 2>&1
clang++ "${FLAGS[@]}" tools/perf_test/facility.cpp source/world/level_spec.cpp -o "$OUT/facility"
"$OUT/facility"
clang++ "${FLAGS[@]}" source/ui/test/projection_test.cpp -o "$OUT/projection"
"$OUT/projection"
clang++ -Itools/perf_test/stubs "${FLAGS[@]}" "${SANITIZERS[@]}" tools/perf_test/autotest.cpp source/core/autotest.cpp tools/nettest/log_host.cpp -o "$OUT/autotest"
"$OUT/autotest"
