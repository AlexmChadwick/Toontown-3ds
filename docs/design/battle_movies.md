# Battle Cinematics & Gag Movies: MovieCamera and Track Implementation

Status: Phase 4+, implemented on branch `battlecam`.

This document details the battle cinematic camera system (`source/world/battle_camera.{h,cpp}`),
the gag track movies (`source/world/battle_movies.{h,cpp}`, `source/world/battle_fx.{h,cpp}`),
sound asset integration, and testing/verification procedures in Azahar and hardware.

---

## 1. Architecture: MovieCamera Port

Upstream Toontown executes combat cinematics through `toontown/battle/MovieCamera.py`, which cuts
between specialized camera angles synchronized to each attack's keyframe timeline.

### 1.1 `BattleCamera` Class

`BattleCamera` is a native C++ port designed for the Old 3DS hardware envelope:
- **No heap allocations per frame**: Uses a static pool of up to 32 shots (`BattleCameraShot shots_[32]`)
  pre-allocated in `source/world/battle_camera.h`.
- **Top Screen 400×240 Framing**: Upstream Panda uses horizontal FOV ~52° on 4:3 displays.
  Citro3d renders the top screen at 400×240 with vertical FOV `kFovYDeg = 55.0f` (`render/renderer.cpp`).
  To prevent toons, Cogs, or overhead props (e.g. pianos, weights) from clipping off-screen, a distance
  scaling factor of `kFramingScale = 1.05f` is applied to camera offsets while keeping the GPU projection
  matrices untouched.
- **Deterministic Synchronization**: All shot choices use `BattleCamRng`, seeded deterministically:
  ```cpp
  rng_.seed(battleId ^ (round * 0x9E3779B9u) ^ 0x55AA3311u);
  ```
  Every client witnessing the same round of combat selects identical camera shots, angles, and split views.

### 1.2 Camera Modes & Shot Types

| Shot Kind (`BattleCamShotKind`) | Description | Coordinate System |
|---|---|---|
| `HeldBattle` | Fixed eye/hpr relative to the battle center | Battle node relative |
| `HeldAvatar` | Fixed eye/hpr relative to a participating toon or Cog | Avatar origin + heading relative |
| `FocusPoint` | Dynamic eye position looking towards an actor or battle point | Battle origin + target point |
| `Shake` | Screen tremor/earthquake with sinusoidal vertical and lateral jitter | Battle origin + shake offsets |
| `FaceOffTaunt` | Cinematic close-up framing the Cog during battle intro | Cog local relative |
| `FaceOffWalk` | Tracking follow-cam as Cog steps up to the battle circle | Battle origin tracking Cog |
| `WaitForInput` | Default overhead battle view framing all 4 toons and 4 Cogs | Battle center |
| `Reward` | Panning victory celebration camera orbiting the local toon | Local toon relative |

---

## 2. Gag Track Movies

All 7 gag tracks are implemented with baked models, vertex animations (`.tta`), and sound effects (`.tsnd`).

### 2.1 Trap (Track 1)
- **Props**: Banana peel (`banana`), Rake (`rake`), Marbles (`marbles`), Quicksand (`quicksand`), Trapdoor (`trapdoor`), TNT (`tnt`), Railroad Tracks (`traintrack`).
- **Mechanics**:
  - Toon places the trap in front of the target Cog during the trap phase.
  - The trap remains persistent in the battle ring between rounds as `battleTrapProp`.
  - When the Cog is subsequently lured (or triggered immediately), the trap activates, damaging the Cog and removing the trap prop.
- **Upstream Timing**: Release at 2500 ms, hit/arm at 2500 ms, duration 3500 ms.
- **Camera Shots**: `chooseTrapShot` selects low-angle all-group view or behind-toon angle focusing on trap placement.
- **Audio**: `trap_banana.tsnd`, `trap_rake.tsnd`, `trap_marbles.tsnd`, `trap_quicksand.tsnd`, `trap_trapdoor.tsnd`, `trap_tnt.tsnd`, `trap_train.tsnd`.

### 2.2 Lure (Track 2)
- **Props**: $1 Bill, Small Magnet, $5 Bill, Big Magnet, $10 Bill, Hypno-Goggles, Presentation Slideshow (`slideshow`).
- **Mechanics**:
  - Toon presents the lure prop. If successful, target Cog(s) slide forward towards the toons by `kSuitLureDistance = 2.6f`.
  - Cog stays lured in the advanced position across rounds.
  - When damaged by another attack (Throw/Squirt/Sound/Drop) or when unlured, the Cog walks back to its battle circle slot.
  - If a trap is present on that Cog's spot, the Cog walks into the trap and triggers it.
- **Upstream Timing**: Lure activation at 3000 ms, attraction/hit at 3200 ms, duration 4500 ms.
- **Camera Shots**: `chooseLureShot` selects over-the-shoulder or split-view framing toon and Cog.
- **Audio**: `lure_1dollar.tsnd`, `lure_magnet.tsnd`, `lure_hypno.tsnd`.

### 2.3 Sound (Track 3)
- **Props**: Bike Horn (`bikehorn`), Whistle (`whistle`), Bugle (`bugle`), Aoogah (`aoogah`), Elephant Trunk (`elephant`), Foghorn (`fog_horn`), Opera Singer (`singing`).
- **Mechanics**:
  - Group attack affecting all active Cogs simultaneously.
  - Toon holds and plays instrument at hand joint (`JNT_wrist_R`).
  - Expanding sound wave visual effect. All target Cogs play cringe / recoil animation.
- **Upstream Timing**: Play sound at 2500 ms, impact/recoil at 2800 ms, duration 3500 ms.
- **Camera Shots**: `chooseSoundShot` selects toon group shot during instrument wind-up, cutting to suit group low-angle on hit.
- **Audio**: `sound_bikehorn.tsnd`, `sound_whistle.tsnd`, `sound_bugle.tsnd`, `sound_aoogah.tsnd`, `sound_elephant.tsnd`, `sound_foghorn.tsnd`, `sound_opera.tsnd`.

### 2.4 Throw (Track 4)
- **Props**: Cupcake (`tart`), Fruit Pie Slice, Cream Pie Slice, Whole Fruit Pie (`fruitpie`), Whole Cream Pie (`creampie`), Birthday Cake, Wedding Cake.
- **Mechanics**:
  - Toon winds up with pie in right hand.
  - Parabolic ballistic arc (`ProjectileArc`) towards Cog head/torso.
  - Impact spawns colored cream splat (`splat_full_action.tta`) and residue texture.
- **Upstream Timing**: Release at 2700 ms, hit at 3000 ms, duration 3500 ms.
- **Camera Shots**: `chooseThrowShot` selects over-the-shoulder follow shot, cutting to Cog impact close-up.
- **Audio**: `pie_throw.tsnd`, `pie_splat.tsnd`.

### 2.5 Squirt (Track 5)
- **Props**: Squirting Flower, Water Glass, Water Gun, Seltzer Bottle, Fire Hose, Storm Cloud, Geyser.
- **Mechanics**:
  - Toon aims and fires squirt stream.
  - Directional water spray particles/cylinder towards Cog.
  - Impact causes water splash particles and Cog soak reaction.
- **Upstream Timing**: Spray at 2500 ms, hit at 2700 ms, duration 3300 ms.
- **Camera Shots**: `chooseSquirtShot` selects dynamic side 3/4 follow shot.
- **Audio**: `squirt_spray.tsnd`, `squirt_splash.tsnd`.

### 2.6 Drop (Track 6)
- **Props**: Flowerpot (`flowerpot`), Sandbag (`sandbag`), Anvil (`anvil`), Big Weight (`weight`), Safe (`safe`), Grand Piano (`piano`), Toontanic Ship (`ship`).
- **Mechanics**:
  - Toon whistles or summons overhead drop.
  - Expanding dark shadow quad appears at Cog's feet on the ground.
  - Heavy prop plummets vertically from `z = 25.0f` down to ground level.
  - On hit: Cog plays squash reaction, screen shakes (`BattleCamShotKind::Shake`).
  - On miss: Cog sidesteps and prop crashes into the ground.
- **Upstream Timing**: Whistle/shadow at 3000 ms, fall impact at 3800 ms, duration 4500 ms.
- **Camera Shots**: `chooseDropShot` selects dramatic low-angle looking up at the falling prop, cutting to impact shake.
- **Audio**: `drop_fall.tsnd`, `drop_impact.tsnd`.

### 2.7 Toon-Up / Heal (Track 0)
- **Props**: Feather (`feather`), Megaphone (`megaphone`), Lipstick (`lipstick`), Bamboo Cane & Hat (`bamboo_cane`), Pixie Dust (`pixiedust`), Juggling Cubes (`cubes`), High Dive Ladder (`ladder`).
- **Mechanics**:
  - Toon performs comedic routine on friendly teammates.
  - Animated feather tickle (`feather_full_action.tta`), juggling cube loops, or megaphone cheering.
  - Healing stars rise around healed toon; Laff meter increments.
- **Upstream Timing**: Routine starts at 2000 ms, laughter/heal applies at 2800 ms, duration 3200 ms (5000 ms for High Dive).
- **Camera Shots**: `chooseHealShot` frames toon team in three-quarter view.
- **Audio**: `heal_tickle.tsnd`, `heal_megaphone.tsnd`, `heal_pixiedust.tsnd`, `heal_juggling.tsnd`.

### 2.8 SOS / NPC (Track 7)
- **Props**: Calling card / telephone animation.
- **Mechanics**: Friendly NPC toon appears in battle circle to grant team-wide effects (restock, full heal, cogs lured, cogs unlured).
- **Camera Shots**: `chooseSOSShot` frames the summoned NPC toon entering the ring.

---

## 3. Autotest Scripting & Azahar Verification

Autotest steps in `source/core/autotest.cpp` permit unattended frame-by-frame verification of every gag movie
without needing live multiplayer network servers.

### 3.1 Autotest Steps

The following steps are registered in `core/autotest.cpp`:

1. **`battle: trap`**:
   - Shot series: `"movie_trap__"` (captures top screen every 0.25 s across 4.0 s).
   - Verifies: Trap placement animation, banana/rake/trapdoor model rendered on ground, camera low-angle framing.
2. **`battle: lure`**:
   - Shot series: `"movie_lure__"` (captures every 0.25 s across 4.5 s).
   - Verifies: $1 bill / hypno goggles prop in hand, split camera shot, Cog sliding forward along battle axis.
3. **`battle: sound`**:
   - Shot series: `"movie_sound__"` (captures every 0.25 s across 4.0 s).
   - Verifies: Instrument prop attached to toon hand, cutting from toon group shot to Cog group cringe shot.
4. **`battle: drop`**:
   - Shot series: `"movie_drop__"` (captures every 0.25 s across 5.0 s).
   - Verifies: Low angle camera looking up, drop shadow expanding on ground, falling anvil/piano, impact camera shake.
5. **`battle: heal`**:
   - Shot series: `"movie_heal__"` (captures every 0.25 s across 4.0 s).
   - Verifies: Feather / megaphone prop animation, team framing shot, heal star particles.

### 3.2 What to Verify in Azahar / Hardware

When reviewing test screenshots or running in Azahar:

- [ ] **Camera Framing**: Verify that the camera never clips through street walls or floor geometry, and participants remain well within the 400×240 viewport at all times.
- [ ] **Prop Scale and Attachment**: Verify that props (instruments, pies, lures) align properly with the right hand wrist joint during animations.
- [ ] **Drop Shadow**: Ensure the circular drop shadow under falling objects smoothly expands from radius 0.2 to full size prior to impact.
- [ ] **Lure Offsets**: Verify that lured Cogs advance towards the center during Lure and walk backwards when hit by subsequent attacks.
- [ ] **Screen Shake**: Verify that Drop impact and Cog earthquake attacks cause subtle, smooth vertical jitter rather than wild camera flipping.
- [ ] **Performance Budget**: Verify frame rate remains locked at 30 fps on Old 3DS with command buffer usage under 60%.
