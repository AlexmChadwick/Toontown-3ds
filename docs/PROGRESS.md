# Progress

## 2026-10-27 — Codex continuation

Done:

- Recovered the paused tutorial-dialogue work from `origin/handoff` onto the writable
  working tree after confirming that the handoff is based directly on `0f38203`.
- Added stock `\a` dialogue-page handling to `TutorialFlow`. Confirmation dialogue now
  remains gated until every page is acknowledged, long pages wrap without splitting a
  UTF-8 code point, and `ClearChat`, completion, and stop discard unread text.
- Added a host flow test that drives all five generated QuestScripts for male and female
  toons, checks timing/event/chat gates and cleanup, and exercises stock and synthetic
  multi-page dialogue.

Verified:

- `git diff --check` passes.
- `tools/tutorial_test/run.sh` reports UNAVAILABLE because `.venv/bin/python` is absent.
- Direct compilation is also unavailable: this Linux workspace has no C/C++ compiler,
  devkitARM, or `make`. No build, sanitizer, emulator, server, or hardware result is claimed.

Next:

- In a provisioned checkout, run `tools/tutorial_test/run.sh`, then the native build with
  zero source warnings.
- Run the Toontorial path against the stock server in Old 3DS Azahar mode, confirm that
  Mickey/Minnie dialogue advances one page per A press and that Tutorial Tom's interior
  loads, and record the measured result in `docs/test-log.md`.
- Commit the working-tree changes and push `codex/work`. This workspace mounts `.git`
  read-only, so Git cannot create `index.lock`; commit and push could not be performed here.

## 2026-10-27 — Toontorial asset delivery follow-up

Done:

- Initialized the pinned `ref/open-toontown` and `ref/resources` shallow submodules and
  checked the native dynamic-zone mapping and required-field parsing against the stock
  `TutorialManagerAI`, `TutorialManager`, and `DistributedTutorialInteriorAI` sources.
- Identified the immediate load failure: the documented full SD bake omitted the canonical
  Toontorial street and interior commands, so `Place::load(20502)` had no installed asset.
- Added NumPy to the documented pipeline environment, added the canonical 20000/20501/20502
  bake commands, and made SD packaging warn when the Toontorial kit is incomplete.

Verified:

- Baked `zones/20000.tz` twice: 6,578 triangles, 11 batches, 1,160,320 bytes, no storage
  errors, unresolved codes, skipped nodes, or warnings.
- Baked `interiors/20501` and `interiors/20502` twice: 1,966/6,018 triangles and 23/69
  batches. SHA-256 hashes of all five runtime geometry/collision files matched across runs.
- The generated-script and five tutorial sender-contract checks pass. The C++ flow and
  dynamic-zone host executables could not run because this image has no C++ compiler.
- `bash -n tools/dev/make_sd_package.sh`, `git diff --check`, and both Python bake commands
  pass. A native 3DS build remains unavailable because devkitARM is not installed.

Next:

- Copy/package the newly baked ignored SD data on a provisioned build host and run the opt-in
  Toontorial path against the stock server in Old 3DS Azahar mode.
- Confirm Tutorial Tom's 20502 interior loads, the generated dialogue pages advance, and the
  20502 -> 20001 -> 20501 -> 20001 -> 2000 route completes; record it in `docs/test-log.md`.
