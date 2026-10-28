# The Toontorial

The tutorial is an opt-in, new-avatar path against the stock `TutorialManagerAI`.
The make-a-toon screen records the player's choice before `createAvatar`; the default is
Skip, which preserves the old front-end behaviour.  Play holds the normal initial zone
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
id just before sending an Astron location update. When an interior returns to the street,
the interest contains both the allocated street and allocated branch; the latter owns the
`DistributedBuilding` objects just as it does on an ordinary street. Canonical PVS entries
are translated at the same boundary. Nothing in the baked data contains a per-player
dynamic id.

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

Like stock `DistributedNPCToon`, the network movie dispatcher replaces the ordinary assignment
balloon for tutorial quests 101 and 110 with `quest_assign_101` and `quest_assign_110`, then sends
`setMovieDone` only when `FINISH_QUEST_MOVIE` runs. Classic-character lines select Mickey or
Minnie from byte 4 of the validated local Toon DNA. While either generated assignment movie
owns the exchange, its dialogue balloon remains anchored to the distributed NPC that sent
`setMovie`; this is separate from Tutorial Tom's special interior object and keeps the HQ
assignment visible after the ordinary talk panel is released.

Stock book and SpeedChat `WAIT_EVENT` prompts are mouse-driven. On 3DS those controls are already
on the persistent bottom screen, so a compact Toontorial panel names the stock action and A
acknowledges it. Generated `ARROWS_ON` chapters add a highlighted-lesson cue; exact named events
remain accepted from touch/UI hooks and the autotest. The purchase panel has higher ownership than
this lesson panel, preserving the real gag grid at `inventory-selection`.

The trolley's stock `NewbiePurchaseManager` (class 177) starts `gag_intro` when the local
avatar is one of its players. The generated script shares `TutorialFlow`, while the 3DS
purchase grid remains visible on the bottom screen. Dialogue appears as the local gag-shop
clerk's fixed top-screen balloon because upstream creates NPC 2011 locally rather than as an
Astron object. Purchase input is locked during the movie except at `inventory-selection`,
where A buys the focused gag; this prevents the same A press used to confirm dialogue from
silently changing the cart, and keeps Done/Cancel unavailable until the stock script enables
the return flow.

## Packet safety and host verification

Every tutorial sender is a `TT_NET_UPDATE` with a generated `TT_SIG` layout assertion:
`requestTutorial`, `toonArrived`, `allDone`, `DistributedTutorialSuit.requestBattle`, and
`DistributedNPCToonBase.avatarEnter`. `tools/tutorial_test/run.sh` regenerates/checks the
scripts against the upstream parser, verifies every sender is generated `clsend` with its
signature guard, and compiles the dynamic-zone mapping vectors.

For an emulator verification (not run by this change), create
`sdmc:/3ds/toontown3ds/autotest_fe` with `shots off`, choose **Play it** in the new prompt,
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
