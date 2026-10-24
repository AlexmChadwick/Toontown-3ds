# Toon animation bake and runtime handoff

2026-10-24; branch `anims`. This is an offline asset/specification change. No
`source/`, runtime format, Makefile, shared reference or virtual environment was
modified. No make, Azahar or hardware test was run; runtime integration remains
for the main agent. The full SD set is this worktree's
`sdmc-out/3ds/toontown3ds/data/toon/` (gitignored).

## Reproduce and audit

Use the worktree's `ref/ref` indirection without modifying the symlinks. Every
Python invocation uses `-I`; `-B`/`--no-cache` also prevent bytecode/model cache
writes. Set numerical/ETC worker counts before imports, and run at most three
bake workers (three CPU workers plus the waiting coordinator):

```sh
export OPENBLAS_NUM_THREADS=1 OMP_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 MKL_NUM_THREADS=1 TT_ETC1_THREADS=1
.venv/bin/python -I -B tools/pipeline/toon_anim_audit.py sources
# In an EMPTY output directory, reproduce the original 17-clip baseline first:
.venv/bin/python -I -B tools/pipeline/bake_toon.py --resources "$PWD/ref/ref/resources" --upstream "$PWD/ref/ref/open-toontown" --no-cache --jobs 3 --legacy-only --out-dir sdmc-out/3ds/toontown3ds/data/toon
.venv/bin/python -I -B tools/pipeline/toon_anim_audit.py snapshot
.venv/bin/python -I -B tools/pipeline/bake_toon.py --resources "$PWD/ref/ref/resources" --upstream "$PWD/ref/ref/open-toontown" --no-cache --jobs 3 --out-dir sdmc-out/3ds/toontown3ds/data/toon
.venv/bin/python -I -B tools/pipeline/toon_anim_audit.py repeat-snapshot
.venv/bin/python -I -B tools/pipeline/bake_toon.py --resources "$PWD/ref/ref/resources" --upstream "$PWD/ref/ref/open-toontown" --no-cache --jobs 3 --out-dir sdmc-out/3ds/toontown3ds/data/toon
.venv/bin/python -I -B tools/pipeline/toon_anim_audit.py verify
.venv/bin/python -I -B tools/pipeline/toon_anim_spec.py
```

The audit refuses to overwrite original hash snapshots. Keep the reports if
repeating a bake: `toon_before_hashes.json`, `toon_before_manifest.json`,
`toon_after_hashes.json`, `toon_sources.json`, `toon-source-ls.log`,
`toon_anim_audit.json`, `toon_clips.tsv`, `toon_determinism.json`.
`--battle-only` retains its original 13-clip selection; `--legacy-only` retains
all original 17. The default full bake adds 34 names. `--available-anims` is on
by default: it omits only the known absent per-style Bow/Curtsy clips;
`--no-available-anims` requests the strict original Cartesian-product behaviour.
Do not use the latter for a full bake with these resources.

## Results and preservation proof

- **547 original binaries / 31,169,024 bytes** SHA-256
  identical before and after: every old TTM, TTA and clothes TTX; zero mismatches.
- **864 new TTA files / 30,210,176 bytes** extra SD storage
  (30.210176 MB, 28.810669 MiB).
- Full set: **1,306 TTA + 104 TTM + 1 TTX**, 61,379,200 bytes
  (58.535767 MiB), plus `toon_manifest.json`.
- An independent repeat of all **1411 binary files** was byte-identical.
- `toon_manifest.json` intentionally grows to list new clips; old part metadata,
  file bytes, file paths, clothing maps and old clip references are unchanged.
- **653 source BAM paths** verified by `ls`, loaded without cache, and
  checked against `Toon.py` phase/suffix mappings and source frame counts. Ten
  Bow/Curtsy combinations are intentionally absent (listed in `toon_sources.json`).
- Every emitted TTA was checked for magic/version, part vertex count, frame count,
  12 fps sampling, section bounds/alignment, finite quantization/attachment data,
  padded vertex lanes, and expected leg/torso attachment tracks.

SHA-256 of the original hash inventory:
`f79a7fba38a3cd006a78f7d923934ccc61f31931f301b64184863fa2340936dd`.
SHA-256 of the complete after inventory:
`07c7b125c9d19ceb5db9d376850d7e2df1b04d290d89f1a8b4f206c315cd3d4c`.

## Clip table

All sources are 24 fps; existing sampling keeps frames 0,2,4,... at 12 fps.
`Src -> baked` gives counts, including variant ranges. `SD KiB` is the total of
that clip across all emitted parts and both LODs. `Part linear KiB` is the
**largest single part file's fully loaded frame allocation**, excluding attachment
heap and file padding. This is measured as `numFrames * numVertices * 8`, matching
`render::Anim::linearBytes()`. `+` means a new clip.

Source path notation: `P<phase>/<prefix><suffix>.bam` expands to
`phase_<phase>/models/char/<prefix><suffix>.bam`. Prefixes are:

| Part | Codes and exact prefixes |
|---|---|
| legs | s `tt_a_chr_dgs_shorts_legs_`; m `tt_a_chr_dgm_shorts_legs_`; l `tt_a_chr_dgl_shorts_legs_` |
| torso shorts | ss `tt_a_chr_dgs_shorts_torso_`; ms `tt_a_chr_dgm_shorts_torso_`; ls `tt_a_chr_dgl_shorts_torso_` |
| torso skirt | sd `tt_a_chr_dgs_skirt_torso_`; md `tt_a_chr_dgm_skirt_torso_`; ld `tt_a_chr_dgl_skirt_torso_` |
| dog head | dls `tt_a_chr_dgm_shorts_head_`; dss `tt_a_chr_dgm_skirt_head_`; dsl `tt_a_chr_dgs_shorts_head_`; dll `tt_a_chr_dgl_shorts_head_` |

Non-dog heads/lashes remain static; they get no invented animation files.
Output names remain `<part>_<code>_<500|250>_<clip>.tta`; each matches the unchanged
`<part>_<code>_<lod>.ttm` vertex order. Attachments remain legs -> hips -> torso
and torso -> head, never a single combined Toon mesh.

| Runtime ID | Clip | Source suffix/path | Src -> baked | SD KiB | Part linear KiB | Feature |
|---:|---|---|---|---:|---:|---|
| 0 | `neutral` | `P3/<prefix>neutral.bam` | 50 -> 25 | 855.38 | 71.48 | standing; Sleepy/Laugh reuse |
| 1 | `walk` | `P3.5/<prefix>walk.bam` | 24 -> 12 | 413.12 | 34.31 | locomotion |
| 2 | `run` | `P3/<prefix>run.bam` | 16 -> 8 | 275.62 | 22.88 | locomotion; cannon ground impact |
| 3 | `jump-idle` | `P3.5/<prefix>jump-zhang.bam` | 23 -> 12 | 413.12 | 34.31 | airborne loop |
| 4 | `wave` | `P3.5/<prefix>wave.bam` | 78 -> 39 | 1329.75 | 111.52 | emote 0 |
| 5 | `throw` | `P3.5/<prefix>pie-throw.bam` | 91 -> 46 | 1568.62 | 131.53 | battle |
| 6 | `pushbutton` | `P3.5/<prefix>press-button.bam` | 96 -> 48 | 1633.38 | 137.25 | battle |
| 7 | `cringe` | `P3.5/<prefix>cringe.bam` | 55 -> 28 | 956.50 | 80.06 | battle; emote 10 |
| 8 | `conked` | `P3.5/<prefix>conked.bam` | 101 -> 51 | 1738.75 | 145.83 | battle; emote 20 subranges |
| 9 | `sidestep-left` | `P3.5/<prefix>sidestep-left.bam` | 87 -> 44 | 1499.88 | 125.81 | battle dodge |
| 10 | `sidestep-right` | `P3.5/<prefix>jump-back-right.bam` | 106 -> 53 | 1804.75 | 151.55 | battle dodge |
| 11 | `duck` | `P3.5/<prefix>duck.bam` | 90 -> 45 | 1533.12 | 128.67 | battle dodge |
| 12 | `victory` | `P3.5/<prefix>victory-dance.bam` | 123 -> 62 | 2112.00 | 177.28 | battle; emotes 6/15 |
| 13 | `lose` | `P5/<prefix>lose.bam` | 164 -> 82 | 2790.00 | 234.47 | battle death |
| 14 | `sad-walk` | `P4/<prefix>losewalk.bam` | 41 -> 21 | 718.00 | 60.05 | battle sad locomotion |
| 15 | `teleport` | `P3.5/<prefix>teleport.bam` | 82 -> 41 | 1398.75 | 117.23 | teleport OUT |
| 16 | `book` | `P3.5/<prefix>book.bam` | 156 -> 78 | 2655.38 | 223.03 | sticker book |
| 17 | `jump` + | `P3.5/<prefix>jump.bam` | 40 -> 20 | 684.75 | 57.19 | Happy; teleport IN |
| 18 | `jump-squat` + | `P3.5/<prefix>jump-zstart.bam` | 20 -> 10 | 345.00 | 28.59 | standing jump start |
| 19 | `jump-land` + | `P3.5/<prefix>jump-zend.bam` | 22 -> 11 | 380.38 | 31.45 | standing jump landing |
| 20 | `shrug` + | `P3.5/<prefix>shrug.bam` | 50 -> 25 | 855.38 | 71.48 | emote 5 |
| 21 | `angry` + | `P3.5/<prefix>angry.bam` | 50 -> 25 | 855.38 | 71.48 | emotes 3/23 |
| 22 | `sit-start` + | `P4/<prefix>intoSit.bam` | 22 -> 11 | 380.38 | 31.45 | trolley intoSit / SitStart |
| 23 | `sit` + | `P4/<prefix>sit.bam` | 1/49 -> 1/25 | 817.88 | 71.48 | trolley seated loop |
| 24 | `swim` + | `P4/<prefix>swim.bam` | 49 -> 25 | 855.38 | 71.48 | ring swimming; cannon flight |
| 25 | `tug-o-war` + | `P4/<prefix>tug-o-war.bam` | 19 -> 10 | 345.00 | 28.59 | tug pull / hold |
| 26 | `sad-neutral` + | `P4/<prefix>sad-neutral.bam` | 49 -> 25 | 855.38 | 71.48 | battle sad standing (not emote 2) |
| 27 | `left` + | `P4/<prefix>left.bam` | 16 -> 8 | 275.62 | 22.88 | emote 22 Delighted |
| 28 | `applause` + | `P4/<prefix>applause.bam` | 51 -> 26 | 888.38 | 74.34 | emote 9 |
| 29 | `confused` + | `P4/<prefix>confused.bam` | 71 -> 36 | 1228.12 | 102.94 | emote 11 |
| 30 | `bow` + | `P4/<prefix>bow.bam` | 81 -> 41 | 907.38 | 117.23 | emote 13 shorts torso |
| 31 | `curtsy` + | `P4/<prefix>curtsy.bam` | 73/81 -> 37/41 | 646.50 | 98.86 | emote 13 skirt torso |
| 32 | `bored` + | `P4/<prefix>bored.bam` | 160 -> 80 | 2720.12 | 228.75 | emote 8 / Yawn |
| 33 | `think` + | `P4/<prefix>think.bam` | 86 -> 43 | 1467.12 | 122.95 | emote 7 subrange |
| 34 | `castlong` + | `P4/<prefix>castlong.bam` | 80 -> 40 | 1362.38 | 114.38 | fishing cast windup |
| 35 | `cast` + | `P4/<prefix>cast.bam` | 95 -> 48 | 1633.38 | 137.25 | fishing release |
| 36 | `fish-neutral` + | `P4/<prefix>fishneutral.bam` | 51 -> 26 | 888.38 | 74.34 | fishing wait loop |
| 37 | `fish-again` + | `P4/<prefix>fishAGAIN.bam` | 33 -> 17 | 583.25 | 48.61 | fishing recovery |
| 38 | `fish-end` + | `P4/<prefix>fishEND.bam` | 33 -> 17 | 583.25 | 48.61 | fishing exit |
| 39 | `reel` + | `P4/<prefix>reel.bam` | 64 -> 32 | 1090.00 | 91.50 | fishing reel |
| 40 | `reel-H` + | `P4/<prefix>reelH.bam` | 87 -> 44 | 1499.88 | 125.81 | additional upstream reel variant |
| 41 | `reel-neutral` + | `P4/<prefix>reelneutral.bam` | 87 -> 44 | 1499.88 | 125.81 | fishing reel recovery |
| 42 | `pole` + | `P4/<prefix>pole.bam` | 40 -> 20 | 684.75 | 57.19 | fishing equip |
| 43 | `pole-neutral` + | `P4/<prefix>poleneutral.bam` | 51 -> 26 | 888.38 | 74.34 | fishing ready loop |
| 44 | `slip-forward` + | `P4/<prefix>slip-forward.bam` | 55 -> 28 | 956.50 | 80.06 | Belly Flop; Catch fallForward; cannon tower hit |
| 45 | `slip-backward` + | `P4/<prefix>slip-backward.bam` | 67 -> 34 | 1159.88 | 97.22 | Banana Peel; Catch fallBack |
| 46 | `catch-neutral` + | `P4/<prefix>gameneutral.bam` | 17 -> 9 | 312.00 | 25.73 | Catch idle |
| 47 | `catch-run` + | `P4/<prefix>gamerun.bam` | 16 -> 8 | 275.62 | 22.88 | Catch locomotion |
| 48 | `catch-eatneutral` + | `P4/<prefix>eat_neutral.bam` | 16 -> 8 | 275.62 | 22.88 | Catch eat while still |
| 49 | `catch-eatnrun` + | `P4/<prefix>eatnrun.bam` | 16 -> 8 | 275.62 | 22.88 | Catch eat while moving |
| 50 | `bad-putt` + | `P6/<prefix>bad-putt.bam` | 60 -> 30 | 1025.25 | 85.78 | emote 21 Cry subrange |

Bow exists for legs, shorts torsos and dls/dsl/dll dog heads; Curtsy exists for
legs, skirt torsos and dss dog heads. Choose Bow/Curtsy from the **torso** DNA,
not the head code. A mismatched dog head uses its neutral mesh/pose while the
torso attachment still moves it; never borrow a different head/torso skeleton.
Curtsy duration varies by the actual torso's source metadata. `sit` also has a
one-frame small-legs variant; its torso/head keep looping.

There are no separate Happy, Sad-emote, Laugh, Yawn, Dance, resistance-salute,
teleport-in/out, eat, fallForward or in-cannon BAMs to duplicate. The aliases and
procedural/subrange rules below are the stock behaviours. `catch-intro-throw`
is listed by Toon.py but some required gameThrow BAMs are absent; this optional
intro clip is excluded. All requested Catch eating/fall variants are present.

## Stock emote ID map

Authoritative function order: `toontown/toon/TTEmote.py:EmoteFunc`; base dispatch
is `otp/avatar/Emote.py`. Human labels/lookup are from
`otp/otpbase/OTPLocalizerEnglish.py:EmoteList/EmoteFuncDict`. There are 25 callable
entries (0..24), with clear sentinel -1. A stock localization quirk lists Laugh
twice: index 16 dispatches **doNothing**, and the duplicate dictionary key resolves
`Laugh` to **24**. Singing names 25..32 exist in localization but have no entries
in TTEmote.EmoteFunc; reject them rather than treating them as playable emotes.

| ID | Label | Clip / action | Loop, duration and cleanup |
|---:|---|---|---|
| 0 | Wave (`doWave`) | wave | one-shot; torso 78/24 = 3.250 s |
| 1 | Happy (`doHappy`) | jump | one-shot; legs 40/24 = 1.667 s |
| 2 | Sad (`doSad`) | current body; sad eyes | 3.000 s; facial-only; restore eyes |
| 3 | Angry (`doAnnoyed`) | angry | one-shot; torso 50/24 = 2.083 s; angry eyes |
| 4 | Sleepy (`doSleep`) | neutral at 0.4x | loop for 4.000 s; close eyes, stop blink/look, sleep thought; restore |
| 5 | Shrug (`doShrug`) | shrug | one-shot; torso 50/24 = 2.083 s |
| 6 | Dance (`doVictory`) | victory | one-shot; legs 123/24 = 5.125 s; not an endless dance loop |
| 7 | Think (`doThink`) | think frames 0..46 then 46..0 | inclusive 47/24 each direction; 3.916667 s total |
| 8 | Bored / Yawn (`doBored`) | bored | one-shot 160/24 = 6.666667 s; sound at 2.2 s, then wait 4.466667 s |
| 9 | Applause (`doApplause`) | applause | one-shot; torso 51/24 = 2.125 s |
| 10 | Cringe (`doCringe`) | cringe | one-shot; torso 55/24 = 2.291667 s |
| 11 | Confused (`doConfused`) | confused | one-shot; torso 71/24 = 2.958333 s |
| 12 | Belly Flop (`doSlipForward`) | slip-forward | one-shot 55/24 = 2.291667 s; sound at 0.7 s |
| 13 | Bow (`doBow`) | bow or curtsy | shorts: 81/24 = 3.375 s; skirt: torso 73/24 or 81/24 = 3.041667 or 3.375 s |
| 14 | Banana Peel (`doSlipBackward`) | slip-backward | one-shot 67/24 = 2.791667 s; sound at 0.7 s |
| 15 | Resistance Salute (`doResistanceSalute`) | victory frames 0..9 then 9..0 at 0.75x | inclusive 10/24/0.75 each way; 1.111111 s; speech CustomSCStrings[4020] |
| 16 | Laugh (legacy no-op) (`doNothing`) | none | 0 s; no track; preserve current animation |
| 17 | Yes (`doYes`) | procedural head pitch | 0.950 s visual track; returned blocking duration is 0 |
| 18 | No (`doNo`) | procedural head heading | 0.950 s visual track; returned blocking duration is 0 |
| 19 | OK (`doOk`) | none | 0 s; no track |
| 20 | Surprise (`doSurprise`) | conked frames 9..50 then 70..101 | one-shot gate 3.000 s; clamp source frame 101 to last valid 100; surprise eyes/muzzle |
| 21 | Cry / Upset (`doUpset`) | bad-putt frames 29..59 reverse then forward at 0.75x | 31/24/0.75 each direction, then hold; gate 4.000 s; sad eyes/muzzle |
| 22 | Delighted (`doDelighted`) | left forward; wait 1 s; left reverse | gate 2.500 s; full inner sequence 16/24 + 1 + 16/24 = 2.333333 s; smile muzzle |
| 23 | Furious (`doFurious`) | angry | one-shot; torso 50/24 = 2.083 s; angry eyes/muzzle |
| 24 | Laugh (`doLaugh`) | neutral at 10x | loop for 2.000 s; laugh muzzle; restore rate 1x |

Only Sleepy and Laugh explicitly loop their body clip, temporarily; they still
end at 4 and 2 seconds respectively. All emotes restore blink/look/expression,
rate and previous animation as their exit track requires, then resume the
previous playing animation, neutral if alive, or sad-neutral if sad. Think,
Salute, Surprise, Cry and Delighted are finite sequences, never modulo loops.
Returned emote durations are **not always the visual sequence duration**: Bored
and slips include a sound delay before the returned wait; Yes/No return zero
but launch an independent 0.95-second visual track. Delighted returns 2.5 seconds
while its internal sequence is 2.333333. Surprise returns 3 seconds and may cut
the internal two-subrange animation short. Stop/cancel all nested tracks on exit.

Procedural Yes head P targets: `-30,+20,-20,+20,-20,+20,0` degrees.
Procedural No head H targets: `+40,-40,+40,-40,+20,-20,0` degrees.
Their segment durations are `0.1,0.15,0.15,0.15,0.15,0.15,0.1` seconds.
Compose these with the torso's head attachment and restore the original head
transform; they are not body clips. Sad only changes eyes for 3 seconds.
The existing baked heads have open eyes/neutral muzzle (bake_toon.fix_head removes
expression muzzles). Facial fidelity requires runtime eye/muzzle support and
separate expression assets; these new body TTAs do not supply those assets.
Do not replace stock Sad-emote with the unrelated whole-body sad-neutral clip.

## Exact runtime integration

1. **Enum/name tables.** Keep `ToonAnim` IDs 0..4 as neutral, walk, run, jump-idle,
   wave. Append IDs 5..50 exactly as the clip table above. Set `kAnimCount` and
   `kToonAnimCount` to **51** and keep the static assertion. Use one shared name
   table for `toon_avatar.cpp::kToonAnimNames` and
   `toon_cache.cpp::kClipNames`, so they cannot drift. The generated handoff
   `sdmc-out/toon_anim_runtime.inc` contains all enum constants, strings and IDs;
   `toon_anim_timing.inc` contains source frame counts/FPS by part/code/clip for
   both LODs (including the Curtsy variants). Apply these to source manually.
   Runtime never reads the JSON manifest.
2. **Cache on demand.** Do not merely enlarge `ToonPart::anim[]` and retain
   `ToonCache::loadPart`'s loop that loads every clip. Key clip requests by
   `(part key, clip ID)`, share them between avatars, and book only required
   active clips with bounded LRU/staging slots. Preserve model/texture sharing,
   the 4 MiB hard budget, loader-thread IO, `LinearGuard`, generation-based
   cancellation and main-thread publication after complete loads. Missing Bow/
   Curtsy/head clips are normal availability cases; a missing body clip must
   select neutral **including its attachment track** so torso/head are not drawn
   at the feet. Never publish a mixed pose/attachment generation.
3. **Bounded frame streaming.** Add a header/file-backed TTA reader to
   `render/Anim` (TTA1 layout stays unchanged) which seeks two sample frames and
   their two attach matrices. Allocate two active frame buffers plus two staging/
   prefetch buffers per resident animated part; reuse preallocated storage.
   Keep source file handles on the loader thread and close on eviction. Flush
   frame data before publication, pin active buffers through frame submission,
   and never overwrite GPU-referenced frames. If IO is late, hold the last
   complete pose and attachment, not a mismatched interpolation pair. Reverse
   intervals and late joins request arbitrary source times, not sequential-only IO.
4. **Playback controls.** Extend `ToonAvatar::play` with explicit mode
   (loop/once/hold/subrange), restart/seek, rate (including negative), start/end,
   and completion/cancel tokens. Current `play` does not restart the same clip;
   repeated emotes/fish casts must be able to restart. Current `sampleClip` always
   wraps: one-shots must clamp a/b at the segment's last sample, never blend the
   last frame back to the first. Keep one synchronized source-time clock for all
   parts, with attachment samples from exactly the same clip/frame pair. Source
   frame `f` is time `f/24`, interpolating the 12 fps samples at `f/2` (odd source
   frames require interpolation). Use source frame counts/timings above for
   completion; `Anim::duration = bakedFrames/12` is up to 1/24 second too long
   when the source count is odd. Hold the last baked key across that short tail.
   Clamp out-of-range upstream Surprise frame 101 to valid source frame 100.
5. **Emote network/event/UI.** Add `setEmoteState` to
   `tools/dcgen/dcgen.py::WANTED['DistributedToon']` and regenerate the header;
   it currently appears only in the skip descriptor, with no named field/sig
   constants. Implement ownsend `DistributedToon.setEmoteState` using the newly
   generated constants (verified field 480, `i16,i16/1000,i16`); never hand-edit
   IDs. Extend the outgoing whitelist and signature assertion, parse fields and
   publish an emote event with ID, multiplier and timestamp. Seek the action
   using the existing centisecond timestamp-wrap/clock correction; do not restart
   remote one-shots on receipt of repeated packets. Validate -1/0..24 before
   indexing, enforce upstream emote-access/disable-body/head rules, clear/cancel
   locally at completion and send -1 as stock does (stock incoming -1 is a no-op).
   Toon.doEmote ignores the received animMultiplier; TTEmote has a separate
   optional volume parameter. Do not multiply fixed track durations by the
   network multiplier. SpeedChat
   label selection must use the function IDs above; name `Laugh` selects 24.
   `setAnimState` still carries whitelisted **state names**, never new clip names.
6. **Feature state bindings.** Apply the exact action rules in the next section;
   preserve the previous locomotion clip/rate and return on completion. Pose-only
   holds freeze sampling, not the whole character/world simulation.

## Feature action rules

- **Trolley:** upstream SitStart/sit-start (`intoSit` source) plays once for
  22/24 seconds, then sit loops. Seat transforms are car-local; no root movement
  is baked into zone space. On leaving, reverse sit-start, then return neutral/
  locomotion as the trolley state machine directs. Stock reverse uses
  startTime=22/24, endTime=0, which Panda treats as inclusive source frames 22..0
  (22 clamps to valid frame 21): 23/24 seconds, not the forward 22/24 duration.
- **Fishing:** `safezone/DistributedFishingSpot.py` (the fishing directory's
  FishBase animations are fish actors, not Toons). Equip: pole once (40/24), then
  pole-neutral loop. `createCastTrack`: castlong at 4x (80/24/4 seconds), then
  cast from source frame 20, then fish-neutral loop. The Cast FSM's separate
  track plays a full cast; do not concatenate both paths. Reel: reel once in
  parallel with the pole actor's frames 63..127, then reel-neutral once, then
  fish-again once, then pole-neutral loop. Exit: fish-end once, hide/drop pole,
  then neutral. reel-H is available as a stock variant but not used by this
  loader's primary reel track. Rod/line/bob transforms and prop animation remain
  separate runtime assets/actions. Existing `battle_toon_extras.py` hand tracks
  cover battle clips only; the new TTAs contain hips/head attachment tracks,
  not animated hand joints. Extend that exporter for the new fishing/Catch clips
  before requiring precise hand-attached rod/fruit props. This Toon clip bake
  does not create those prop/joint assets.
- **Catch:** Catching selects catch-neutral / catch-run (negative play rate for
  reverse). CatchEating selects catch-eatneutral / catch-eatnrun according to
  locomotion, then returns after the idle eating duration (16/24 seconds).
  FallForward/FallBack select slip-forward/slip-backward, retimed by
  `playRate=(N-1-12)/24` for nominal one-second falls. CatchGameToonSD passes
  `startTime=12/(N-13)`, `endTime=N/(N-13)`; Panda multiplies those by its
  rate-adjusted FPS, yielding inclusive source frames **12..N**, with N clamped
  to N-1. Actual duration is **(N-11)/(N-13)** seconds: 44/42=1.047619 for forward
  (N=55), 56/54=1.037037 for backward (N=67). Preserve this stock timing quirk,
  rather than treating the full clip as one second. Fruit attaches to the hand, shrinks/hides separately.
- **Cannon:** in-cannon is a **head-only** representation, not a new body clip.
  Hide/detach the full body, place the head at barrel-local `(0,6,0)`, HPR
  `(0,-45,0)`, preserving Toon scale. Launch restores the full body, loops swim,
  offsets it by `-toonHeight/2` under a trajectory parent, and follows trajectory
  rotation. Tower hit holds slip-forward source frame 25; water hit loops neutral;
  ground impact loops run at 2x with parent pitch -150. No `in-cannon.tta` exists.
- **Ring:** loop swim, with upstream swimming/world bob/rotation separate from
  vertex frames. **Tug:** loop tug-o-war while pulling; when not pulling, hold
  source frame 3 (interpolated baked frame 1.5), rather than resetting to neutral.
- **Teleport:** OUT is existing teleport once, explicitly 3.4 seconds, with hole/
  hand reparenting at 0 and 1.708 seconds, sound at 0.5, cleanup at 3.4. IN does
  **not** reverse teleport: initially pose its last frame hidden; wait 0.3 seconds,
  show body/nametag and play jump from source time 0.45 (baked sample 5.4), while
  the hole prop runs 3.4->3.1, waits 0.6, then 3.1->3.4. Total hole sequence 1.2 s;
  Toon jump determines any longer completion. No duplicate teleport-in/out TTAs
  are emitted. Clip playback alone does not implement the hole or clipping plane.
- **Jump/battle sadness:** jump-squat once -> existing jump-idle loop -> jump-land
  once -> locomotion for standing jumps. The full jump is used for Happy and
  teleport-in. Running leap variants are outside this bake. Existing lose and
  sad-walk are unchanged; sad-neutral loops for the Sad state, sad-walk forward
  rate 1.2 and reverse rate -1 as Toon.enterSad defines.

## Memory plan and practical limits

The largest new fully loaded part is Bored: **234,240 bytes (228.75 KiB)** linear;
its attachment allocation is at most 80*48 = 3,840 regular-heap bytes. Largest
existing part remains Lose: 240,096 bytes. All 51 fully loaded clips for the
single default dog/m/ms at LOD500 already cost **7,261,384 linear bytes**, excluding
its models/textures/clothes; eagerly loading them violates the 4 MiB Toon pool.
Even one full action per nine different Toons can consume most of that pool.

Measured maximum vertices are legs 114, torso 366, head 258. Two sample frames
for nine complete animated Toons cost at most **106,272 bytes**; an additional
two-frame staging window doubles that to **212,544 bytes**, before model/texture
buffers and allocator alignment. 48 cache parts at the maximum 366 vertices and
four buffers cost at most **562,176 bytes** linear. Four attach matrices cost
192 heap bytes per attaching part; headers/handles and request metadata are also
regular heap. These figures are reservations, not a device timing measurement.

A bounded streaming implementation must charge its frame/staging allocations and
all existing models, clothing and fallback against 4 MiB; cap resident parts,
evict unreferenced actions, decline prefetch or use the existing LOD250/fallback
when admission fails. Never depend on New 3DS memory. Frame interpolation stays
on the GPU; no runtime BAM/Python/DNA parsing or whole-catalog allocation is added.
Only the new SD footprint grows. The offline audit and determinism proofs do not
claim runtime frame time, SD latency, facial rendering or hardware validation.


## Runtime implemented on branch `animrt` (2026-10-24)

The handoff above is implemented for the supplied body/attachment clips. Generated
`source/world/toon_clips.inc` and `toon_timing.inc` expose all 51 stable IDs and
24 fps source durations, including sparse combinations. Regenerate with
`.venv/bin/python -I -B tools/gen_toon_anims.py`; the generator audits the linked
reference BAM headers first. IDs 0..4 remain unchanged.

`ToonCache` retains shared models/clothing and neutral two-key attachment poses.
Its 12 fixed stream groups each contain three parts, two active frames and two
staging frames (512 vertices/part). Matching published part keys and frame pairs
are shared between avatars, with independent interpolation weights; other times
book bounded owner slots. A loader owns at most 36 clip handles, reads arbitrary
source samples, and publishes all three parts together through a generation check.
Missing or invalid clips select neutral with attachments. Late IO retains the
last complete pose. `pump()` must run before `C3D_FrameBegin`: it fences the prior
GPU frame before staging swaps or retired-resource reuse. `LinearGuard` protects
allocations/deletions. Model, clothing, neutral and stream reservations all count
against admission; referenced resources cannot be evicted to exceed the cap.

`ToonAvatar::playAction` supports Loop/Once/Hold, explicit restart, start/end source
times, negative rate and elapsed seek; `finished`, `token`, and `cancel` expose
completion/cancellation. `ToonClipSet::load` now books names, and the existing
battle `playClip` interface routes through the same pool. It no longer loads whole
battle files synchronously. One-shot sampling clamps; vertex and attachment keys
and interpolation always agree. Odd-length source loops retain the short tail
interval rather than adding an extra baked sample period.

SpeedChat's Emotes submenu has 24 entries (stock IDs 0..24 except legacy no-op 16).
The server's 25-byte access list gates picks and outgoing calls. Existing linked
phrase emotes also dispatch publicly through whitelisted `setEmoteState` (field
480, `i16,i16/1000,i16`); clear is -1. The regenerated DC hash is **547155168**.
Remote RAM/spawn and live events seek by the converted network timestamp; exact
duplicates and stale samples are ignored. The multiplier is ignored for incoming
emotes, as stock `Toon.doEmote` does. Body emotes lock local movement; Yes/No rotate
the head procedurally. Eye-only, eyelid, muzzle and sound variants need separate
facial/audio assets and are not synthesized from body TTAs.

Feature hooks bind trolley sit-start -> sit; fishing equip/cast/reel/exit; Catch
eating, local anvil forward fall and timestamped incoming suit-hit backward fall;
Cannon head-only aiming, trajectory swim, tower frame-25 hold, ground run and
water neutral; Ring/Diving swim; Tug frame-3 hold/pull; and teleport-out followed
by hidden 0.3 s teleport-in, then jump from 0.45 s. The existing Catch scene has no
local suit collision/render implementation; the backward fall consumes its
existing network event. Teleport hole/clipping/hand choreography, new precise
rod/fruit hand tracks and facial effects still require the separate assets named
above. This change implements the Toon animation hooks and preserves the current
prop rendering; it does not create those missing assets.

### Runtime budget and validation

| Resource | Bound / measured value |
|---|---:|
| Entire Toon linear cache, hard admission cap | 4,194,304 bytes |
| Preallocated active + staging frame windows | 589,824 bytes (576 KiB) |
| Remaining cap for shared models, clothes and neutral windows | 3,604,480 bytes |
| Stream attach matrices, regular heap | 6,912 bytes |
| Neutral attachment buffers, regular heap | up to 4,608 bytes (48 x 96) |
| Loader stack, regular heap | 32,768 bytes |
| Open streamed clip files | at most 36 (+ clothing pack) |
| Instrumented host stress peak, synthetic models/textures | 4,000,128 bytes |

Metadata, model tables and libc file buffers also use regular heap. The synthetic
peak verifies admission rather than predicting real scene usage. No whole action
file or catalog is resident, and streaming requests perform no linear allocation.
Old 3DS FPS, heap/linear/VRAM peaks and SD latency remain device measurements.

`tools/anim_test/run.py` builds ASan/UBSan tests of playback math and the real
Anim/ToonCache code against instrumented host GPU/model stubs. It checks source
periods, reverse/subranges/hold, restart/cancel generations, timestamp wrap/seek,
all emote mappings/menu IDs, shared frame pairs, sparse neutral attachments,
truncation refusal, no repeated-request allocations, GPU fencing and hard-cap
admission. Existing network vectors/single/two-client/reconnect tests, eight
minigame conformance suites, fishing, Toon DNA, shop, tailor, classic characters,
projection, zone format units and terrain/assembled-zone collision pass. Full
suit-path reference tests require the unavailable `panda3d.toontown` server fork;
full zone-integrity tests require the external baked `.tz` set. Neither was
regenerated or claimed as run. The required `make -j4` builds with zero source
warnings; the toolchain linker emits its existing crtn.o GNU-stack warning.

### Main-agent Azahar verification (not run here)

1. Install the full `anims` Toon SD set under
   `sdmc:/3ds/toontown3ds/data/toon/` (same file names as the table); RomFS alone
   contains only the boot clips. Run Old 3DS mode with one local and eight nearby
   remote Toons of mixed species, clothing and LODs. Watch the cache/debug memory
   report stay <=4 MiB for Toons, total linear <=24 MiB, animation <=6 ms and
   30 fps; record actual heap/linear/VRAM and frame time in the test log. Change
   DNA/LOD and zones repeatedly; verify no GPU panic, leaked files or rising memory.
2. Pick all unlocked Emotes by touch and D-pad, including scrolling to Laugh (24).
   Verify locked entries do nothing, linked phrases still send, Yes/No move only
   the head, Think/Salute/Upset reverse/subranges return correctly, Bow/Curtsy keep
   head and torso attached, and repeating the same emote restarts. With a PC
   client observe both directions and inject a delayed timestamp: the remote
   body should start partway through, without an end-to-start one-shot blend.
3. Board the trolley: sit-start then sit. Fish through equip, charge/cast,
   waiting, reel/reel-neutral/fish-again, cancel and leave; verify each action
   restarts on another cast and movement resumes after exiting. Expect the
   existing approximate rod placement until the new hand tracks are baked.
4. Play Catch: idle/run/eat and anvil fall; receive a PC player's suit hit and
   observe the backward fall. Test Cannon head in the barrel, launch swim and
   tower/ground/water impacts; Ring/Diving swim; Tug idle frame-3 hold and pull.
5. Map-teleport and return from a minigame: 3.4 s out, arrival hidden for 0.3 s,
   jump starting at 0.45 s, then movement returns. Tunnel/door walking must still
   work. Hole/hand clipping choreography is a separately identified asset task.
6. Fight a Cog through throw, pushbutton, cringe, sidestep, victory and lose using
   the existing battle movie. Confirm clothes and attachments stay correct while
   battle clips stream, and moving resumes after the reward. Remove one action
   TTA, repeat an action and verify neutral fallback without detached body parts.
