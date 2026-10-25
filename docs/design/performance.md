# Performance work (perf branch)

Target: Old 3DS, 30 fps, main-thread busy below 20 ms. This work is host/build
verified only; Azahar and hardware measurements are intentionally left to the
main agent. Host timings are not 3DS timings and do not prove the target.

## Instrumentation

All frame owners expose update/draw/load/wait/end/audio costs. Early-return tails
are `other`, never `audio`. Long profiler windows split every six fields so no
section silently disappears from a truncated log. `stats:` includes actual uniform
upload API calls and texture binds, as well as draws, triangles and command usage.
The deterministic host clock test checks exclusive attribution and busy minus wait.

## Ordered renderer state reuse

Previously each model invalidated texture, tint, blend, layer and cull caches.
Now the cache spans consecutive models within one world pass. Program changes
invalidate the tint uniform; beginWorld/endWorld invalidate everything. Draw order
is unchanged, including alpha and depth-biased facial layers. A host trace of 128
shared-model instances retains exactly the same ordered draw stream while reducing
tint uploads 128→1 and texture-state changes 128→1. Real bind/upload counters are
required to confirm the benefit with scene assets in Azahar.

## Matrix specialization

`world * dequantize` now uses its known diagonal-plus-translation form rather than
a general matrix multiply and temporary matrix setup. The final view multiply is
unchanged. 10,000 random equivalence cases pass (maximum tolerance 0.0001 in render
matrix elements; no collision/gameplay math changed). On this arm64 host, one
million iterations measured 13.04→11.23 ns/draw (1.16×). Compiler and hardware
matter; use the same harness for a fresh local comparison.

## Exact persistent text layout

Ui, FrontEnd and BattlePanel now retain parsed glyphs for equal UTF-8 strings.
Position, scale, colour and word-wrap draw flags are still applied at draw time.
Their original glyph buffers/capacities are unchanged; a full buffer invalidates
both glyphs and all keys together. Every render target is still cleared every
frame. Existing revision/dirty caches in other panels are retained.

The exact fixed hash-key harness checks collision resolution, UTF-8, full capacity,
content changes and reset. Across 10,000 unchanged 80-label frames, layout calls
fall from 800,000 to 80; lookup costs 34.91 ns/string on this host. Each key table
uses 26,116 bytes plus 128 C2D_Text records. No allocation is added to frame paths.
The front end already changes preview DNA only when its selection/DNA changes;
there was no per-frame model rebuild to remove.
