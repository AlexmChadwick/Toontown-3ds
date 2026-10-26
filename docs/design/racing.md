# Goofy Speedway — racing2

Native C++ runtime and offline original assets for the pinned unmodified Open Toontown server.
Host/build verification is recorded below; no Azahar or hardware run was performed by this branch.

## Stock path and authority

A fresh toon has KartDNA body -1 and 200 tickets (`DistributedToonAI.py:181–185`).
The starter body costs 100. `DistributedNPCKartClerkAI.py:60–83` requires the
current clerk session, validates tickets and changes the authoritative body/ticket fields.
The client requests this purchase and waits for those owner resends. It never grants a kart.

`DistributedRacePadAI.considerAllAboard:95–112` permits one racer in Practice;
ToonBattle and Circuit require at least two. Practice costs zero. Stock pads change
track signs every 15 seconds, so the diagnostic waits for forward Screwball Practice.
`ToontownAIRepository` creates RaceManagerAI and the DNA starting blocks. There is
no server-side blocker to this flow and no AI modification or forced-game switch is needed.

Pad states, starting-block occupancy, enter/exit movie acknowledgments and the five
localized refusals follow `DistributedStartingBlock`, `DistributedKartPad`,
`DistributedRacePad` and `KartShopGlobals`. Countdown is 30 seconds and AllAboard
is six. Exit requests are allowed only while the cancellation button exists upstream.
The movie clock includes original interval lengths and Ogg engine-start sample counts.

Race setup waits for the actual track, vehicle links and published kart parts before
acknowledging waitingForJoin / waitingForPrep / waitingForReady. readRules lasts nine
seconds within the AI's ten-second barrier. startRace's network timestamp controls the
three-second countdown. The vehicle's Controlled state and our ownership/driver ID
are required before publishing its smooth-node pose. Pose is sent on the vehicle DO,
not substituted with an avatar pose. Progress is sent every 500 ms and on lap changes.

The local finish line stops driving and waits for `setPlace`. The result panel shows
server time/place/qualification/entry fee/winnings/bonus and authoritative tickets.
No client code changes tickets. A confirms the exit barrier and racerLeft; the client
returns to 8000. Intermediate Circuit results wait for the AI's setRaceZone instead.
`RaceManagerAI.racerFinished` pays only qualifying racers; forward Screwball allows
240 seconds and PracticeWinnings is 20. The host diagnostic finishes three laps in
211.6 seconds; this is a physics test, not evidence of a live server payout.

## Physics, geometry and presentation

`racing_logic.cpp` implements the stock proRacer=0 DistributedVehicle control path:
60 Hz mean-velocity Euler integration, acceleration from pitch, 40-degree turn ratio,
slide/lean, per-surface resistance, ice steering/acceleration and delayed three-second
turbo. The declared upstream maxSpeed=200 is not the actual velocity clamp: the
stock path clamps at 600. Four original wheel-base locators drive pitch/roll from
floor rays. The native radius-four wall sphere removes blocked velocity and uses
0.1 dynamic friction. Its bounded horizontal wall solver is an approximation of
Panda's PhysicsCollisionHandler, not a claim of bit-exact Panda collision response.

Each track retains its original identity transform and real floor/wall/camera solids.
The old pad-collision placeholder is removed. 4,000 original curve samples, start and
middle locators, unsorted start-position enumeration, original gag placements and
floor surface tags are baked in TRC1. Reverse tracks use curve_reverse/lap_start_rev
and add 180 degrees to the start heading. Upstream raceWatcher interpolates using
squared projected length; that legacy behavior is retained. The lap middle locator
is recorded, but upstream does not enforce a separate middle-checkpoint latch.
Grid positions behind the line start at lap -1, so the first crossing is not a free lap.

All three KartDNA bodies and all three upstream LODs are present, with every one
of the 65 mesh accessories (engine, spoiler, wheel wells, rims and decals). Colors
remain draw uniforms. Four bounded seven-component kart instances stream through
the shared loader and publish at the frame fence; no LinearGuard surrounds load.
LOD transitions have hysteresis. Seat and wheel anchors are generated from the BAMs.
Toons retain their DNA and use sit playback. Remote vehicles accept all smooth-node
packet variants, interpolate continuously and receive kart-based nametags.

The bottom panel contains the curve minimap, four racer dots/list, lap/place, time,
wrong-way warning, gag slot and results. The existing top race HUD remains active.
A/Circle Pad accelerates, B reverses, Circle Pad X steers, R uses a ready gag.
The kart chase camera uses the upstream (0,-33,16) offset and camera-wall pull-in.

RaceGag pickup probability uses the original GagFreq[place][index], with the stock
index-zero inactive behavior preserved. Pickups shuffle for two seconds. Banana,
turbo, anvil and pie requests use the actual race fields. Dropped hazards report
hitSomebody on the correct distributed object. A bounded 16-shot ring replays pie
broadcasts; native pie motion follows Piejectile's 60 Hz forces, homing, lifetimes and
seven-unit target hit. Homing splats remain cosmetic, as upstream; dropped-pie hits
and banana wipeouts damp driving. Anvils use the original drop timing and model.

Presentation limits for follow-up visual review: the 51-clip toon catalog contains
jump/sit, but not feedPet; the neutral interval occupies that flourish during boarding.
Kart wheels/accessories are baked rigid components, so independent wheel spin and
steering and random suspension shake are not reproduced. Billboard/particle splats,
engine pitch audio, the portal flourish and detailed RaceEndPanels trophy art are
not included. These are known fidelity gaps, not an emulator-tested claim of full parity.
The unused DistributedProjectile prototype requests Panda's models/smiley, which is
not in the resource checkout; its protocol is bounded/decoded but no invented model
is shipped. The stock race uses RaceGag, DistributedGag and Piejectile instead.

## Binary metadata and budgets

TRC1 little endian: 32-byte header `<4sHHIffIII>` = magic, version=1, track ID,
4000 points, normalized start/middle, start/gag/floor counts. Then points (4 floats:
XYZ/surface), starts (6 floats XYZ/HPR), gags (3 floats), floor triangles (10 floats:
ABC coordinates/surface). Counts, track ID, finite values, truncation and trailing
bytes are validated. Surface codes are asphalt/gravel/dirt/grass/ice/unknown = 0..5.

Both directions have the same complete-track geometry/texture costs:

| Track IDs | Track | Triangles | Draw batches | Geometry B | Textures B | TTC file B | TRC heap B |
|---|---|---:|---:|---:|---:|---:|---:|
| 0/1 | Screwball Stadium (RT_Speedway_1_rev) | 1550 | 20 | 55268 | 267936 | 41488 | 78640 |
| 20/21 | Rustic Raceway (RT_Rural_1_rev) | 4071 | 28 | 140426 | 382464 | 184608 | 122640 |
| 40/41 | City Circuit (RT_Urban_1_rev) | 2914 | 16 | 156172 | 210688 | 112832 | 103840 |
| 60/61 | Corkscrew Coliseum | 2011 | 15 | 90834 | 202464 | 71216 | 91360 |
| 62/63 | Airborne Acres | 6201 | 27 | 261142 | 360512 | 317104 | 130760 |
| 64/65 | Blizzard Boulevard | 2542 | 12 | 134164 | 176576 | 113616 | 105400 |

Worst equipped kart per LOD (independent maxima across bodies):

| LOD | Tris | Batches | Geometry B | Textures B |
|---|---:|---:|---:|---:|
| 0 | 1549 | 21 | 30302 | 235936 |
| 1 | 1231 | 24 | 25018 | 252288 |
| 2 | 1231 | 24 | 25018 | 230464 |

Four karts plus the largest track and all four shared props conservatively use
387,932 B geometry and 1,425,024 B textures (independent maxima), within the
6 MiB geometry / 8 MiB texture zone pools. The toon cache keeps its existing hard
4 MiB cap. The maximum native TTC allocation plus TRC points/floors is 458,080 B
regular heap. Network pools are fixed: 24 pads, 96 blocks, 16 vehicles, 32 hazards,
four racers, eight barriers, 32 pickups and 16 pie broadcasts. No per-frame heap
containers are introduced. Shared loader staging and existing zone/UI/audio pools
retain their budgets.

Track-only costs are measured offline, not a whole-frame claim. Four co-located
fully equipped karts can add 6,196 triangles / 96 batches before toons/gags. Thus
some worst scenes can exceed the provisional 15k / 150 envelope. Renderer culling
helps ordinary views; Old 3DS FPS, command-buffer usage, complete-frame draw cost
and stereo still require Azahar/hardware measurement. No 30 FPS claim is made here.

## Reproduce and install

Run from this worktree; all Python is isolated and reference trees stay read-only:

```sh
.venv/bin/python -I -B tools/pipeline/bake_racing.py
.venv/bin/python -I -B tools/pipeline/bake_racing_karts.py --lod 0
.venv/bin/python -I -B tools/pipeline/bake_racing_karts.py --lod 1
.venv/bin/python -I -B tools/pipeline/bake_racing_karts.py --lod 2
.venv/bin/python -I -B tools/pipeline/bake_racing_props.py
.venv/bin/python -I -B tools/pipeline/gen_racing_tables.py
bash tools/racing_test/run.sh
.venv/bin/python -I -B tools/racing_test/determinism.py
DEVKITPRO=$HOME/devkitpro DEVKITARM=$HOME/devkitpro/devkitARM make -j4
```

Install the committed `sdmc-out/3ds/toontown3ds/data/racing/` tree at
`sdmc:/3ds/toontown3ds/data/racing/`, including karts/lod0, lod1 and lod2.
There are 634 generated binary assets: 36 track files, 594 kart parts, four props.
Keep the existing complete SD base installed: Speedway zone 8000, Kart Shop
interior 8501 and toon sit/jump clips are prerequisites. The existing interior baker
can produce the shop with `--resources ref/ref/resources --zone 8501 --no-preview`.
No source patch is required by the main agent beyond integrating these commits.

## Verification and exact Azahar checklist

Host results: actual upstream __watchControls with stock Panda physics compared
against six ten-second native cases, max component error 0.024903; actual upstream
Piejectile method compared for two three-second cases, max error 0.000977.
ASan/UBSan cover forward/reverse/grid laps, all 14 outgoing DC packings against
Panda, send guards, truncated results/shots and bounded shot storage. All 4,800
original Panda floor probes match native hit presence/heights and surface tags.
The diagnostic uses physical input only and finishes three original Screwball laps
in 211.6 seconds, inside the stock 240-second qualifying limit. Fresh-process bakes
prove all 634 binaries byte-identical. Per-file hashes/budgets are in racing_assets.json.

Existing regression runner: 41/42 pass; suit is UNAVAILABLE (77), requiring the
missing tools/server/.venv with panda3d.toontown. Stock wheels do not supply it.
The additional perf and cog-attack shell suites are run separately. This environment
limit remains explicit; it is not counted as a passing suite.

Main agent should verify in Azahar Old 3DS mode, then record actual frame/memory
numbers in docs/test-log.md:

1. Fresh toon: 200 tickets, no kart. Touch an available starting block before buying;
   confirm the exact localized no-kart refusal and no ownership/ticket mutation.
2. Run `go speedway`, `speedway: pads`, `speedway: race`, `speedway: shop` (same labels,
   pads bounded to 180 seconds, race to 600). Pads visits the real tb1 shop door and
   clerk, purchases body 0 for 100, waits for ownership and exits through the door.
   Race waits for forward Screwball Practice. Shop records final owner/ticket state.
3. Require `racing autotest: request stock starter kart cost 100`, then
   `owned kart 0 tickets 100 ready for Practice` on a fresh account. The server alone
   authorizes those values; an existing account can legitimately differ.
4. Require pad WaitCountdown/AllAboard, block movie, original start, and every barrier
   acknowledgment in log: waitingForJoin, waitingForPrep, waitingForReady, readRules.
   Check the countdown does not permit early motion and B cancels only while boarding.
5. Require increasing `racing: lap`, server `racing: result avatar ...` and authoritative
   tickets, then `racing autotest: authoritative result and return complete`. A fresh
   qualifying Practice race should have a 20-ticket award (record bonus may add more).
   `FAIL returned without a server result` means the diagnostic did not pass.
6. Manually drive backwards, hit walls/banks, test gravel/grass/ice on appropriate
   tracks, all twelve IDs/directions and camera obstruction. Compare seat/body colors,
   equipped parts and all three LODs. Test four players for smooth karts and nametags.
7. In a two-player ToonBattle race test pickup/index-zero behavior, the two-second
   shuffle, R banana/turbo/anvil/pie, hazards and the authoritative result for each
   player. Practice deliberately has no gag pickups. Test Circuit transitions when
   the stock holiday exposes them; no AI switch is introduced.
8. Repeat races/exit/purchase/disconnect and check resource retirement, no stale kart
   parts, no black bottom screen, command-buffer peaks and no heap/linear growth.
   Capture full-frame triangles/draw calls/FPS and heap/linear/VRAM, including stereo.
