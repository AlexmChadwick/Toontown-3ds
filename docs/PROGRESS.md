# Progress

## 2026-10-27 — Toontorial control-cue polish

Done:

- Added context-specific bottom-screen cues for every blocking Toontorial action: dialogue,
  Shticker Book open/close and pages, SpeedChat, and the Newbie Gag Shop selection.
- Made generated `ARROWS_ON` chapters visible as a highlighted-lesson cue on 3DS. The real
  purchase panel retains ownership during gag selection, so this guidance does not replace the
  purchase grid or weaken its input lock.
- Extended the host flow assertions and source-contract check to cover the new prompts.

Verified:

- The isolated QuestScript generator check and all tutorial sender/UI source contracts pass.
- `bash -n tools/tutorial_test/run.sh` and `git diff --check` pass.
- The sanitizer-backed flow executable cannot be built because this image has no C++ compiler.
  The native 3DS build is also unavailable because `DEVKITARM` is unset; no emulator, server, or
  hardware result is claimed.

Known issues:

- The complete opt-in Toontorial still needs its first Old 3DS Azahar play-through against the
  stock server, including a 320x240 review of these cues and the Gag Shop selection hand-off.
- The compact tutorial still omits the desktop client's animated arrows, GUI props, and camera
  choreography; the new panel communicates their actions without fabricating replacement assets.
- Real Old 3DS performance and memory high-water marks remain unmeasured.

Next:

- On a provisioned host, run the complete tutorial harness and native build, package the canonical
  tutorial assets, then play the full route in Old 3DS Azahar mode and record it in the test log.
- If the live pass is sound, replace only the highest-value textual cues with baked original GUI
  art where it remains readable and within the Old 3DS texture budget.

## 2026-10-27 — Newbie Gag Shop tutorial integration

Done:

- Connected the stock class-177 `NewbiePurchaseManager` to the generated `gag_intro`
  QuestScript when the local avatar is one of the manager's players. The local-only gag-shop
  clerk now presents its dialogue in a stable top-screen balloon; it never sends the NPC
  `setMovieDone` acknowledgement because upstream's NPC 2011 is not a distributed object.
- Preserved the stock purchase teaching lock on the 3DS bottom screen. Dialogue A presses no
  longer buy gags accidentally, Done/Cancel remain unavailable during the movie, and only the
  focused gag purchase is accepted at the generated `inventory-selection` gate.
- Prevented trolley autotest from submitting the purchase while the tutorial owns it, and added
  source-contract coverage for dispatch, local-player filtering, input gating, and autoplay.

Verified:

- The generated QuestScripts check and all five tutorial sender/source contracts pass.
- `bash -n tools/tutorial_test/run.sh` and `git diff --check` pass.
- The sanitizer-backed host flow executable could not be built because this image has no C++
  compiler. The native 3DS build is also unavailable because `DEVKITARM` is unset; `make -n`
  stops at the toolchain guard. No emulator, live-server, or hardware result is claimed.

Known issues:

- The complete opt-in Toontorial, including the first trolley ride and class-177 Gag Shop movie,
  still needs its first Old 3DS Azahar play-through against the stock server. Confirm that a gag
  can be selected at the prompt, purchase exit completes, and quest 110 advances on return.
- The compact 3DS movie uses the original dialogue and timing but does not render upstream's
  animated NPC head, arrows, button throbs, or advanced-gag preview.
- Real Old 3DS performance and memory high-water marks remain unmeasured.

Next:

- On a provisioned host, run `tools/tutorial_test/run.sh`, build with zero warnings, package the
  canonical tutorial assets, and play the full route in Old 3DS Azahar mode.
- Review the Gag Shop tutorial at 400x240/320x240 and add the missing decorative cues where they
  materially improve clarity without compromising the Old 3DS memory and draw budgets.

## 2026-10-27 — Toontorial quest-movie integration

Done:

- Matched stock `DistributedNPCToon` for the two tutorial assignment movies: quest 101 and
  quest 110 now run their generated QuestScripts instead of falling back to ordinary ToonTask
  balloons, and `FINISH_QUEST_MOVIE` sends `setMovieDone` to the owning NPC exactly once.
- Selected Mickey/Minnie dialogue from the validated local Toon DNA. Female toons no longer
  receive the hard-coded male classic-character branch.
- Made stock Shticker Book and SpeedChat `WAIT_EVENT` gates reachable by a player: A confirms the
  highlighted action on the persistent 3DS bottom screen, while exact named UI/autotest events
  remain supported. NPC collision cannot start another conversation while a script owns the UI.
- Added source-contract checks and a host regression case for the 3DS event acknowledgement.

Verified:

- `git diff --check`, the isolated QuestScript generator check, `sh -n` on the tutorial runner,
  and the new quest-dispatch/gender/event source contracts pass.
- The sanitizer-backed C++ flow executable could not be built: this image has no C++ compiler,
  and installing `g++` is blocked by the unprivileged package-manager lock. The native 3DS build
  remains unavailable because `DEVKITARM` is unset. No emulator, server, or hardware result is
  claimed.

Known issues:

- The complete opt-in Toontorial still needs its first Old 3DS Azahar play-through against the
  stock server. Verify quest 101/110 presentation and acknowledgements, both dynamic-street
  returns, the Flunky battle, HQ/book prompts, blocker SpeedChat prompts, and the TTC tunnel.
- `gag_intro` is generated and host-tested as a script, but the later NewbiePurchaseManager path
  does not yet dispatch it through `TutorialFlow`.
- The compact 3DS presentation retains stock dialogue and ordering but does not yet render the
  decorative tutorial GUI props/camera choreography represented by no-op QuestScript commands.
- Real Old 3DS performance and memory high-water marks remain unmeasured.

Next:

- On a provisioned host, run `tools/tutorial_test/run.sh`, build with zero warnings, package the
  canonical tutorial assets, and execute the full route in Old 3DS Azahar mode.
- Integrate `gag_intro` with NewbiePurchaseManager and review every scripted prompt at 320x240
  before claiming the Toontorial complete.

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

## 2026-10-27 — Toontorial dynamic street interest fix

Done:

- Fixed the canonical/dynamic zone boundary when either tutorial interior returns to the
  tutorial street. The allocated street ID is outside the ordinary street-ID range, so the
  old generic check opened only the street interest and omitted the allocated branch that owns
  its `DistributedBuilding` objects.
- Tutorial street interests now include the server-allocated branch and translate canonical PVS
  entries before sending them. Added source-contract coverage and documented the interest rule.

Verified:

- `git diff --check` passes.
- `.venv/bin/python -I tools/gen_quest_scripts.py --check` passes.
- The tutorial dynamic-interest source-contract checks pass.
- A native build is unavailable in this Linux image: `make` stops because `DEVKITARM` is unset,
  and the image has no host C++ compiler. No emulator, live-server, or hardware result is claimed.

Known issues:

- The complete opt-in Toontorial still needs its first Old 3DS Azahar play-through against the
  stock server. In particular, verify both returns to dynamic street `20001`, the HQ visit, the
  final TTC tunnel, dialogue paging, and that buildings remain generated after each transition.
- Real Old 3DS performance and memory high-water marks remain unmeasured.

Next:

- On a provisioned host, run `tools/tutorial_test/run.sh`, build with zero source warnings,
  package the canonical tutorial assets, and execute the full tutorial route in Azahar.
- Record the live result and resource measurements in `docs/test-log.md`; fix any presentation or
  protocol issue found before claiming the Toontorial complete.
