# The Toontorial

The tutorial is the default new-avatar path against the stock `TutorialManagerAI`, as in
the original (NameShop.__isFirstTime): an account's first Toon goes straight in; later Toons
get the PromptTutorial screen (Enter Toontorial / Skip Toontorial; B goes back, it never
skips).  The choice is recorded before `createAvatar`.  Play holds the normal initial zone
entry, opens the management interest, and sends only `TutorialManager.requestTutorial`
after the owner view has arrived.  `enterTutorial` supplies four dynamic zone ids.

## Dynamic zones and bakes

`TutorialManagerAI.__createTutorial` allocates `branch`, `street`, `shop`, and `hq` at
runtime. `world/tutorial_zones.h` maps them at the network boundary to canonical assets:

| Server zone | Canonical place | Bake |
| --- | --- | --- |
| branch | 20000 | no object is generated in it |
| street | 20001 | `sdmc:/3ds/toontown3ds/data/zones/20000.tz` |
| shop | 20502 | `sdmc:/3ds/toontown3ds/data/interiors/20502.{ttm,ttc,pts}` |
| HQ | 20501 | `sdmc:/3ds/toontown3ds/data/interiors/20501.{ttm,ttc,pts}` |

The canonical street is `phase_3.5/dna/tutorial_street.dna`; its sole visgroup is 20001.
The interior ids are the stock `branch + 500 + block` calculation: `tb2` (Tutorial Tom)
and `tb1` (HQ). `Client::changeZone` converts canonical destinations back to the dynamic
id just before sending an Astron location update. Nothing in the baked data contains a
per-player dynamic id.

Bake the assets from this worktree (Python is isolated):

```sh
.venv/bin/python -I tools/pipeline/bake_street.py --zone 20000
.venv/bin/python -I tools/pipeline/bake_interior.py --tutorial
```

`bake_interior.py --tutorial` uses `storage_tutorial.dna` and `storage_interior.dna`,
reproduces the street view through Tom's windows, and applies the tutorial HQ's hidden
periscope/speakers. The outputs deliberately remain SD data, not tracked source assets.

## QuestScripts

`tools/gen_quest_scripts.py` extracts the stock QuestParser tokenizer with `ast`, reads
the five tutorial callers from `phase_3/etc/QuestScripts.txt`, and emits
`source/quest/quest_scripts.{h,cpp}`. It rejects unknown commands, variables and argument
layouts. Run ` .venv/bin/python -I tools/gen_quest_scripts.py --check` in CI, or omit
`--check` to regenerate.

`quest/ScriptPlayer` preserves source order, `WAIT`, background ease-in/out lerps,
confirm-chat pages, `WAIT_EVENT`, preview timing, and `UPON_TIMEOUT` cleanup.
`world/TutorialFlow` supplies the 3DS stage: dialogue is routed to the existing speech
presentation, arrows are represented by its `arrows()` flag, and transforms/color state
are retained for the scene owner. Commands whose Panda target has no 3DS equivalent
(`LOAD`, reparenting, bins, book obscuring, and music-volume intervals) are executed in
order but intentionally have no extra visual object; inventory/book actions remain on the
bottom-screen UI. This is the deliberate visual simplification, not fabricated script data.

Where each script starts (as upstream):

| Script | Trigger (upstream) | 3DS |
| --- | --- | --- |
| `tutorial_mickey` | DistributedTutorialInterior generate | `TutorialInterior` event; Minnie's lines for a girl Toon |
| `quest_assign_101` | Tom's `QUEST_MOVIE_ASSIGN` 101 (DistributedNPCToon.setMovie, `questDefined`) | `Talk` enters `Phase::Script`, main starts the script; its end sends `setMovieDone` |
| `quest_assign_110` | HQ Harry's `QUEST_MOVIE_ASSIGN` 110 | same as 101 |
| `tutorial_blocker` | Flippy's `BLOCKER_MOVIE_START`, after `avatarEnter` on touching his (4.5, 1, 6) scaled tube | Flippy stands at `NPCToons.BlockerPositions`; contact sends `avatarEnter` once |

While a script runs the toon stands still (the `quest` place state). Its lines show in the
speaker's balloon and on the bottom-screen talk panel (A or a tap dismisses a line). Tom's
lesson cards are the real `tutorial_gui` art (`tools/pipeline/bake_tutorial_gui.py` ->
`romfs/baked/tutorial_gui.ttm`), drawn under his words while the script has a card parented to
the camera.

## Packet safety and host verification

Every tutorial sender is a `TT_NET_UPDATE` with a generated `TT_SIG` layout assertion:
`requestTutorial`, `toonArrived`, `allDone`, `DistributedTutorialSuit.requestBattle`, and
`DistributedNPCToonBase.avatarEnter`. `tools/tutorial_test/run.sh` regenerates/checks the
scripts against the upstream parser, verifies every sender is generated `clsend` with its
signature guard, and compiles the dynamic-zone mapping vectors.

For an emulator verification (not run by this change), create
`sdmc:/3ds/toontown3ds/autotest_fe` with `shots off` and `tutorial` (a fresh account's first
Toon enters the Toontorial on Create!; add `skip` for the old skip path),
and inspect `sdmc:/3ds/toontown3ds/log.txt` for these lines in order:

```
net: requested the Toontorial for new toon
net: enterTutorial branch ... street ... shop ... hq ...
net: toonArrived (Toontorial)
tutorial: script tutorial_mickey start
net: tutorial Flunky
net: tutorial battle
net: allDone (Toontorial finished)
```

The normal `autotest_fe` choice remains Skip and must show `net: requested tutorial skip`.
The emulator is intentionally not started by host tests.
