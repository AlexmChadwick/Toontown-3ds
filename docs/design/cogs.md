# Cog and battle asset handoff

This change implements AGENT_TASK.md's offline asset work. It does not integrate
combat or change any runtime file. All generated assets and previews are ignored
under this worktree's `sdmc-out/`. No Azahar or `make` was run, as explicitly
required by the task; device FPS, heap, linear-memory and VRAM measurements are
not available. The memory figures below are measured allocation bounds from the
baked files, not hardware observations.

## Reproduce

From this worktree, with the existing stock panda3d 1.10.15 environment:

```sh
.venv/bin/python -I tools/pipeline/bake_battle.py
# Independent stages:
.venv/bin/python -I tools/pipeline/bake_battle.py --stage toon
.venv/bin/python -I tools/pipeline/bake_battle.py --stage suit
.venv/bin/python -I tools/pipeline/bake_battle.py --stage props
.venv/bin/python -I tools/pipeline/bake_battle.py --stage audio
.venv/bin/python -I tools/pipeline/bake_battle.py --stage verify
.venv/bin/python -I tools/pipeline/bake_battle.py --stage preview
```

The driver uses two fresh single-threaded Toon workers; other stages use one
process. Numerical-library limits are set before NumPy import. ffmpeg decoder,
encoder and filter threads are explicitly limited to one. All entry points disable
Python bytecode writes. Panda's disk cache is disabled. There are no package
installs, writes to shared references, emulator invocations or build invocations.

The supplied reference layout has empty `ref/open-toontown` and `ref/resources`
submodule directories and an existing `ref/ref` symlink to the shared references.
`battle_common.py` resolves this read-only layout, while also supporting the normal
`ref/{open-toontown,resources}` layout. Nothing under `ref/` or `.venv` is changed.
`battle_toon.py` supplies that resolution to the unchanged Toon bake functions and
routes their worker subprocesses through the same wrapper.

`bake_toon.py` changes only the ANIMS table and the `--battle-only` option. That
option includes neutral as the unchanged base-mesh pose; the full battle driver
bakes both legacy and battle clips. No existing mesh extraction, texture selection,
vertex ordering, animation sampling or worker implementation is changed. The
before/after hashes cover every old model, eyelash mesh and old animation output:
**234 files, all byte-identical**. The seven Make-A-Toon clothing textures use the
existing clothing writer unchanged. The old RomFS audio list and its disk budget
remain unchanged; new SD battle entries are separate `BATTLE_ASSETS`.

Only the explicitly allowed new scripts, this document, and the two permitted
existing pipeline files are part of the commit. No push or merge is performed.

## Outputs and manifests

| Directory under `sdmc-out/3ds/toontown3ds/data/` | Contents |
|---|---|
| `suit/` | 24 body TTM files, 32 head TTM files, 288 TTA clips, 288 JNT tracks, department and skeleton-tie TTX packs, suit manifest, battle budget and global hash manifest |
| `toon/` | all 104 existing meshes, 130 unchanged legacy clips, 1,032 added battle clips, 144 hand tracks, Make-A-Toon clothes pack and updated Toon manifest |
| `props/` | 20 gag/effect meshes, five animated-prop clips, one inventory-atlas TTM, atlas UV/gag JSON and prop manifest |
| `audio/` | 28 TSND files and battle audio manifest |

`battle_manifest.json` is the versioned global inventory, with the byte length and
SHA-256 of every shipping root file and links to the specialized manifests. Staging
`toon/before/` and optional `repeat/` directories are not shipping assets. Existing
legacy baseline and verification JSON files are retained for auditability.

**Formats remain unchanged:** TTM1 v1 (`ttm.py`), TTA1 v1 (`tta.py`), TTX1 v1
(`bake_toon.py`) and TSND v1 (`audio_tsnd.py`). No BAM, DNA, JPEG, Ogg, Python or
Panda library is introduced into the native client. All textures are pre-tiled,
with existing ETC1/ETC1A4/L8/LA8/A8 formats and mip rules. Inventory atlas has a
single mip level to avoid adjacent-icon bleed.

New **JNT1 v1** sidecars provide prop attachment matrices, using regular heap or
streamed I/O rather than GPU vertex memory:

```
32-byte header, little endian:
  char[4] magic = "JNT1"
  u16 version = 1; u16 jointCount
  u32 frameCount; f32 fps = 12
  u32 dataOffset; u32 frameStride = jointCount * 48
  u32 nameTableOffset = 32; u32 reserved = 0
Names: jointCount * 32 bytes, NUL-padded UTF-8
Data: 128-byte-aligned f32[frameCount][jointCount][3][4]
```

Matrices use column vectors, translation in column 3. Suit body clips also carry
their head matrix in the **existing TTA attach section**; JNT is needed only for
additional hand/shadow joints. Toon torso clips retain their existing head attach
tracks and add `def_joint_left_hold` / `def_joint_right_hold` JNT files. See
`battle_common.write_joints` and `battle_toon_extras.py` for the writers.

## Suits, departments and attachment chain

`SuitDNA.py` supplies all 32 head codes, body membership and department ordering.
`suit_source.py` reads its literal tables, evaluates only Suit.generateSuit's
configuration method with side-effect-free stubs, and records each head's part
list, hand color, head texture/color override, scale and height. It imports no
upstream game classes. All 32 specifications are in `suit_manifest.json`.

Source references, relative to `ref/open-toontown/` (or `ref/ref/open-toontown/`
in this worktree):

- `toontown/suit/SuitDNA.py:10-108`: head codes, body classes, departments, hand colors.
- `toontown/suit/Suit.py:11-13,394-610`: base sizes, species scales, heights, head parts
  and overrides. Flunky includes both `flunky` and `glasses`; Corporate Raider,
  Bottom Feeder, Bloodsucker, Double Talker, Spin Doctor, Mingler, Name Dropper and
  Robber Baron receive their specified replacement texture; Cold Caller receives
  its head color override.
- `Suit.py:611-670`: body model and animation source selection.
- `Suit.py:681-700`: department blazer, sleeve, leg textures; hands and hold joints.
- `Suit.py:755-778`: head pieces attach under `joint_head` with their local transforms.
- `Suit.py:905-937`: separate lose actor, with the head reattached to its neck.
- `Suit.py:947-973`: optional skeleton actor, scale factor 1.0173 and two-sided planes.
- `Suit.py:780-800`: skeleton department tie textures.

Body names are `suitA`, `suitB`, `suitC`; suffixes are `_full` and `_low`. Full
ordinary bodies preserve source triangles. A deterministic short-edge collapse
reduces each ordinary low body to at most 327 triangles, leaving room for the
largest head. All 32 static heads are shared across full and low bodies: they are
already only 7–72 triangles. Low LOD begins **beyond 45 world units**. Head atlas
resolution is the largest of 256/128/64 that fits a 64 KiB head-texture allowance;
complete body-plus-head texture use is checked against 96 KiB. This retains source
face detail where palette texture dimensions and formats allow it.

Each collapse creates a fixed linear map from source vertices to LOD vertices.
Exactly the same map is applied to every animation frame. It never changes vertex
ordering per pose or removes material batches. Full and low skeleton variants are
also reduced as needed to meet 1,500 and 400 triangles respectively. Rest-position
UVs/colors are interpolated by the collapse; the low mesh intentionally approximates
the original silhouette and texture layout at distance.

The complete attachment chain is:

```
actor world translation and Z-heading
  * uniform species scale (Suit.py scale, not the displayed height)
    * body vertex animation
    * interpolated body TTA head attach matrix
      * head_<dna-code>.ttm, local head transforms already baked
```

Apply the species scale once to both body and head, not separately to the head
matrix's translation. Head geometry is static; animation comes from its attachment.
Nametag world height is the manifest height + 1, matching Suit.setHeight. Skeletons
have their own built-in skull, so **do not attach a normal Cog head**; apply the
additional 1.0173 scale in the runtime. Skeleton lose models are separate too.

`departments.ttx` reuses TTX1's four table counts, each **3**. The table identities
are **c, l, m, s**, rather than Toon shirt/sleeve/shorts/skirt. Within each table:

| TTM texture slot | Material | Department table entry |
|---:|---|---:|
| 1 | torso / blazer | 0 |
| 2 | arms / sleeve | 1 |
| 3 | legs | 2 |
| 4 | optional skeleton tie | use the separate `skeleton_ties.ttx` table entry 0 |

`skeleton_ties.ttx` has four one-entry department tables in the same c/l/m/s order.
Skeleton batches referencing their original tie texture are marked slot 4, including
lose variants. Embedded textures remain valid fallbacks. No body mesh is duplicated
per department. TTM **tint slot 4** is reserved for suit **hands**: source RGB is
neutralized to white, and SuitActor supplies the manifest's SuitDNA hand color.
Texture slot 4 and tint slot 4 are independent fields. Toon rendering continues to
use its existing tint-slot meanings. Head overrides are already baked, so use white
uniform tint for the static head.

## Animation and movie assets

Bodies and skeletons have 23 clips per LOD: neutral, walk, pie-small-react,
squirt-small-react, lose, victory, sidestep-left/right, slip-forward, flail, landing,
and 12 selected generic attacks. Lose variants have only their proper lose clip.
Sources follow Suit.py's phase-specific tables: A/B neutral/walk/tutorial clips in
phase 4, C in phase 3.5; minigame/flail clips in phase 4; landing/sidesteps in phase 5.
Attack sources use each species' suffix and phase from Suit.py's per-species tables.
They are not guessed by searching for a matching filename.

`SuitBattleGlobals.py:2889+` maps attack names to animations. Selection counts how
many attack entries across species of that body use each available animation,
sorts by frequency then name, and retains 12. The manifest records the counts,
actual source path for every clip, and an explicit per-species attack fallback map.
Unselected attacks use `throw-paper` as the generic visual; damage/targeting still
comes from the authoritative movie. No claim is made that 12 animations reproduce
every bespoke suit attack prop/particle sequence.

The added Toon mapping follows `toontown/toon/Toon.py:51-80,86,139`:

| Runtime clip | Source suffix | Phase |
|---|---|---:|
| throw | pie-throw | 3.5 |
| pushbutton | press-button | 3.5 |
| cringe, conked | same name | 3.5 |
| sidestep-left | sidestep-left | 3.5 |
| sidestep-right | jump-back-right | 3.5 |
| duck | duck | 3.5 |
| victory | victory-dance | 3.5 |
| lose | lose | 5 |
| sad-walk | losewalk | 4 |
| teleport, book | same name | 3.5 |

These clips cover all three leg sizes, six torso/clothing shapes and four animated
dog head shapes, at 500 and 250. The other 30 head shapes are rigid upstream; each
gets an explicit one-frame, 12 Hz TTA for every battle clip, holding its decoded
static head positions. The torso attach track supplies their head motion. A runtime
may keep these heads static and omit loading their redundant one-frame files.
All animation names match the existing `<part>_<code>_<lod>_<anim>.tta` convention.

Suit/prop source frames are sampled at exact 12 Hz, interpolating adjacent source
positions/matrices where necessary; duration is rounded up by less than one bake
tick. Every source pose checks batch states, indices, vertex count and UV/color
order. Joint matrices are summed in stable joint-name order in float64 to avoid
Panda's pointer-order float32 blend-sum drift; the unchanged extractor then emits
geometry. Unreferenced source rows are removed consistently. Negative joint scales
in death/effect animations must not change winding or vertex order, so animated
morph batches use stable source winding and two-sided drawing throughout.

The existing Toon worker samples source clips at its original 12 Hz rule without
changing any old output. Validation confirms all generated TTA headers say 12 Hz.
Toon meshes and their pre-existing vertex ordering remain unchanged.

**Props:** feather, megaphone, lipstick, lips, hat, cane, tart/cupcake,
fruitpie-slice, creampie-slice, squirting-flower, glass, water-gun, bottle,
splat, button, propeller, kapow, splash, phone and receiver. Feather, glass, splat,
propeller and kapow have vertex clips. Sources come from BattleProps.py:5-194 and
its kapow/tart variants at :255-287. The table's phase-3.5 splash path is absent in
the pinned resources; the shipped `phase_5/models/props/splash.bam` is used instead.
Tart's 0.5 scale and lips' -3.04 Z offset are baked through an identity holder.
The manifest supplies the source splat variant scales and colors for the first
three Throw gags and splash-from-splat (BattleProps.py:198-205,300-312).

Toon-Up levels 0–2 (player levels 1–3) are feather, joke/megaphone and smooch:
MovieHeal.py:34-44,133-161,232-252. The joke itself is chat text. Hat/cane are also
provided for the next dance gag, eliminating ambiguity around level numbering.
Throw and Squirt levels 0–2 use their listed first three props; the next Squirt
bottle is included too. Particle sprays, sound-wave particles and procedural object
trajectories are runtime movie effects, not positions baked into these prop models.

The single inventory atlas is `inventory_atlas.ttm` (one ETC1A4 texture and a dummy
quad for the existing model loader) with `inventory_atlas.json`. It includes all
50 source icon GeomNodes in a 256×256 atlas, 28×28 interiors in padded 32×32 cells,
no mipmaps, clamp wrapping. JSON records named rectangles, half-texel-inset UVs,
and all 49 track/level associations from ToontownBattleGlobals.AvPropsNew:529-578.
UV t=1 is the image top, matching existing texture tiling. Expose its texture to
the bottom-screen sprite renderer; do not draw the dummy world quad in the UI.

**Audio:** two looping battle music tracks are stereo 32 kHz DSP-ADPCM with the
existing block/loop predictor contexts. The other 26 cues are mono 22.05 kHz PCM16:
pie throw, tart/slice impacts, throw miss, flower/glass/water-gun squirt hit/miss,
hangup, Cog death, propeller in/out, cogfall-apart, Toon hit/bodyfall, and all nine
species' howl/ouch vocals. Every source filename, duration, sample count, encoding
and lossy music SNR is in `battle_audio_manifest.json`. There is no generic stock
`ouch.ogg`; species howls are the existing Toon dialogue hurt vocals. `toon_hit`
is the shipped factory hit cue; `Toon_bodyfall_synergy` is used by the battle movie.
Do not assume these offline exports implement full species dialogue routing.

## Runtime work required

1. Add **SuitActor**, mirroring ToonAvatar's model/morph draw path. Select body/head,
   department, hand tint, species scale and height from the suit manifest/generated
   fixed tables. Decode the server's existing SuitDNA fields in native code;
   keep head codes and source IDs authoritative. Use low LOD beyond 45 units.
2. Add shared department/tie TTX loading and batch texture-slot overrides. Reuse the
   existing tint uniform array, supplying Suit hand color at tint slot 4. Cache
   body/head/department resources independently; batches remain merged by material.
3. On death, switch to the corresponding `_lose` body and lose TTA, retaining its
   department/head/scale and sampling that body's head attachment. Skeleton death
   selects `_skeleton_lose`, with no normal head. Do not use only the ordinary
   body's lose clip for the disassembly geometry (Suit.py:905-937).
4. Add **bounded two-frame TTA streaming**. The current Anim::load in
   `source/render/anim.cpp:48-86` allocates the entire clip in linear memory.
   That cannot meet this battle budget: four worst-case full Cog active clips
   alone exceed 6 MiB. Read the unchanged 64-byte TTA header, retain scales/offset,
   read adjacent `frameStride` blocks into two aligned reusable linear buffers,
   flush them, and bind those pointers in drawMorph. Read/seek JNT and TTA attach
   tracks into regular memory. Serialize linear alloc/free under LinearGuard.
   Reuse buffers across clips; release inactive ToonCache locomotion clips when
   entering battle. A loader thread must never race allocations or frame buffers
   in use by the GPU. Prefetch enough SD data; the static allocation bound does not
   prove SD latency or frame timing on hardware.
5. Loop neutral/walk; clamp reactions, lose, landing and action clips to their last
   frame. Existing sampleClip always wraps. A SuitActor clip-state flag and clamped
   sample time can avoid wrapping one-shot death/actions without changing TTA.
   Schedule impacts in seconds from the server movie; example source timings are
   Throw release 2.7 / hit 3.0 and Squirt spray 2.5 / hit 2.7
   (`docs/design/combat.md` §6). Fly-in/out positioning is still procedural.
6. Read JNT1 hand tracks for Toon/suit prop placement. Compose the interpolated
   matrix with its torso/body parent and movie-local prop offset, rather than
   attaching every prop to the actor origin. Parent-held props use joint space;
   launched props switch to battle/world space at their release time.
7. Load the icon atlas into the bottom-screen gag picker using the JSON's native
   track/level mapping. Implement it as fixed/generated tables or a small loader,
   not as per-frame JSON parsing.
8. Stream one music track through the existing **128 KiB** ring, and bound the new
   SFX cache to **four resident cues**, evicting only after their voices stop.
   The budget reserves four times the largest PCM cue payload, covering duplicates if
   shared buffers are not deduplicated. All 26 SFX together exceed 2 MiB; do not
   preload them all into the audio pool. Use the recorded loop/predictor contexts.
9. Battle protocol/state, authoritative HP, gag ownership and movie acknowledgments
   remain the main client's responsibility. This bake adds no outgoing fields.
   Complete the Old 3DS Azahar/hardware tests after integration, logging actual
   FPS, heap, linear and VRAM values. Render targets, world geometry, command
   buffers and unrelated UI pools remain governed by their existing budgets.

## Measurements

The tables below are generated from the final manifests. KiB = 1,024 bytes;
MiB = 1,048,576 bytes. Texture bytes include all native mip levels. Ordinary actor
triangle/texture bounds include its largest possible head for that body. Skeleton
bounds include their built-in skull. These are per-actor maxima, not additive
across unrelated body variants.

| Actor | LOD | Complete actor triangles | Texture KiB | Model KiB | Largest clip KiB |
|---|---|---:|---:|---:|---:|
| a | full | 467 | 59.9 | 27.8 | 356.2 |
| a | low | 362 | 59.9 | 25.9 | 289.9 |
| a_lose | full | 465 | 59.9 | 27.6 | 340.9 |
| a_lose | low | 362 | 59.9 | 25.9 | 279.1 |
| a_skeleton | full | 1336 | 29.8 | 60.8 | 1156.0 |
| a_skeleton | low | 394 | 29.8 | 40.6 | 390.6 |
| a_skeleton_lose | full | 1336 | 29.8 | 60.8 | 1101.0 |
| a_skeleton_lose | low | 397 | 29.8 | 40.5 | 366.6 |
| b | full | 481 | 81.2 | 27.4 | 358.8 |
| b | low | 393 | 81.2 | 25.8 | 295.8 |
| b_lose | full | 499 | 83.9 | 30.8 | 349.5 |
| b_lose | low | 394 | 83.9 | 28.6 | 276.0 |
| b_skeleton | full | 1494 | 32.5 | 69.8 | 1487.0 |
| b_skeleton | low | 396 | 32.5 | 45.0 | 492.4 |
| b_skeleton_lose | full | 1496 | 32.5 | 69.8 | 1366.6 |
| b_skeleton_lose | low | 397 | 32.5 | 44.6 | 436.9 |
| c | full | 479 | 59.9 | 27.9 | 380.0 |
| c | low | 374 | 59.9 | 25.9 | 303.4 |
| c_lose | full | 479 | 62.6 | 30.8 | 351.8 |
| c_lose | low | 375 | 62.6 | 28.6 | 277.6 |
| c_skeleton | full | 1492 | 29.8 | 63.2 | 1289.5 |
| c_skeleton | low | 397 | 29.8 | 40.4 | 395.2 |
| c_skeleton_lose | full | 1492 | 29.8 | 63.2 | 1183.0 |
| c_skeleton_lose | low | 397 | 29.8 | 40.5 | 365.8 |

| Resident allocation | KiB |
|---|---:|
| all_twenty_prop_models_and_two_frames | 126.9 |
| allocation_and_transition_reserve | 256.0 |
| department_pack | 33.6 |
| four_cogs_models_and_two_frames | 423.0 |
| four_sfx_slots_largest_cue | 815.0 |
| four_toons_models_and_two_frames | 848.0 |
| gag_icon_atlas | 64.9 |
| music_ring | 128.0 |
| skeleton_tie_pack | 11.2 |
| toon_clothes_pack | 17.1 |
| **Total / 4096 KiB limit** | **2723.8** |
| Remaining | 1372.2 |

## Offline verification — 2026-10-24

All 181 meshes, 1,455 clips, 432 joint tracks, three texture packs and 28 audio files pass binary validation. All 234 pre-existing Toon outputs match their pre-change SHA-256 hashes. Independent fresh-process repeats match every Cog, Toon, prop/icon and audio output byte-for-byte: toon: 1414 files, props: 28 files, audio: 29 files, suit: 635 files. Six mid-walk source/full/low comparisons cover every body and multiple departments; the contact sheet and image metrics are under `sdmc-out/previews/cogs/`. No make, Azahar or hardware test was run.

The whole-scene allocation is **2.660 MiB**, conditional on the documented two-frame animation streaming and four-cue audio cache. The current full-clip runtime loader does not satisfy that plan. Generated outputs remain ignored under `sdmc-out`; rerun `bake_battle.py` after cherry-picking the code.
