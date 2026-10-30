# Progress log

Dated entries: what was done, how it was verified, and what is next. Test numbers live in
`docs/test-log.md`; architecture and decisions in `AGENTS.md`.

## 2026-10-30 — Sharper exterior building and prop textures

### Done
- Fixed a root cause of the reviewed blurry/indistinct exterior art: the street atlas reduced
  nearly every original building, roof, wall, sidewalk, tree and prop texture to 32 x 32. The
  production bake now preserves 48 x 48 tiles, 2.25 times as many source texels, while retaining
  the existing 256 x 256 ETC1/ETC1A4 PICA pages, four-pixel gutters and UV clipping.
- Parameterized every atlas placement and UV calculation instead of scattering new dimensions.
  The baker retains an explicit `--atlas-tile 32` comparison mode, so the old and new packed `.tz`
  output can be captured from identical generated cameras; 48 is the shipping default and the
  only default used by single-zone and `all` subprocess bakes.
- Extended the zone regression to assert both the shipping 48-pixel UV span and legacy 32-pixel
  A/B span in addition to repeat/clamp geometry area, alpha-only sign masks and oversized UVs.
  Original DNA sign lettering remains a standalone texture at up to 256 pixels and is not reduced.

### Verified
- Panda3D 1.10.15 focused zone suite: all six format/atlas/sign tests pass. Python byte-compilation
  and `git diff --check` pass.
- Fresh zone 2000 production bake: worst planned view is 150 draws, 14,213 triangles, 750 KiB
  geometry and 4.27 MiB textures; the hard caps remain 150 / 15,000 / 6 MiB / 8 MiB.
- Fresh shop-heavy zone 2300 matched A/B bakes and decoded packed-TZ previews: 32-pixel planned
  residency was 89 draws / 2.80 MiB textures; 48-pixel is 130 draws / 4.18 MiB. The same-camera
  frames show finer fence grain, tree bark/canopy, sidewalk edges and distant facade detail with
  unchanged geometry/sign placement. The 48-pixel bake remains below every enforced budget.
- A full 3DS build still cannot run in this workspace because `DEVKITARM` is absent. No Azahar or
  hardware performance result is claimed.

### Asset size decisions
- No original art was replaced or generated. The higher-resolution bake keeps the original game
  textures and spends available Old 3DS zone-texture headroom on visible detail. Zone 2300's pooled
  file grows only from 5.45 MiB to 5.65 MiB because TZ v3 stores each distinct page once.

### Known issues
- All 25 exterior zones need a production rebake before packaging; each bake's per-cell planner
  must pass rather than assuming the representative TTC and shop-street margins apply everywhere.
- The matched previews are offline packed-output renders. Native 400 x 240 Azahar captures and an
  Old 3DS residency/frame-time pass remain required, especially in TTC where the draw plan reaches
  its exact 150-draw world cap before actors.
- The reviewed warped shop roof/black-slash case needs its exact zone/block and same-camera native
  capture to distinguish any remaining mesh/winding defect from the now-fixed texture reduction.

### Next
- Re-bake all exteriors, run the complete zone loader/corruption suite, and update the measured
  zone table with the 48-pixel planned budgets.
- Capture the reported TTC shop plus representative flat buildings, landmark shops, props and the
  local Toon at 400 x 240; fix any remaining UV, winding, filtering or decal-order defect from those
  exact frames.
- Run the requested live low-level/developed battle gag picker and SpeedChat capture, then continue
  the original-asset Fire and NPC SOS presentation pass.

## 2026-10-30 — Shticker Book SOS card polish and restored gag regression

### Done
- Replaced the SOS book's generic square table with rounded parchment cards matching the live
  battle picker. Each card now has a stable gag-track/status colour spine, compact effect heading,
  NPC name, strength, and a high-contrast calls badge, so unlike rewards can be scanned at native
  bottom-screen size.
- Corrected the player-facing rarity display. Open Toontown stores SOS rarity in six zero-based
  tiers (0..5); the page now renders one to six pips instead of making tier-zero Field Office
  rewards look as though their rating failed to draw.
- Repaired the potency regression introduced with focused-gag descriptions. Its AST environment
  now evaluates upstream `UberSkill`, `regMaxSkill`, and `MaxSkill`, including the max-level gag
  entries that previously stopped the test before it checked anything.

### Verified
- `python3 tools/gag_data_test.py` checks all 514,549 possible stock gag potency inputs against
  the native integer formula with no mismatch.
- Regenerated all 41 SOS definitions twice from the pinned Open Toontown checkout and compared
  byte-for-byte; `python3 -m py_compile` and `git diff --check` pass.
- `make -j2` reaches the repository toolchain guard and stops because this workspace has no
  `DEVKITARM`. No emulator or hardware result is claimed.

### Asset size decisions
- No images, textures, heap allocations, or SD data were added. The page reuses the existing
  immediate rounded-triangle primitive and generated SOS table, leaving Old 3DS residency and
  install size unchanged.

### Known issues
- The eight-card page needs a native 320 x 240 capture with the longest names and all six rarity
  tiers, plus D-pad page-flip and touch verification on Old 3DS hardware.
- NPC SOS still lacks the temporary DNA-driven Toon, teleport-in greeting, and teleport-out
  presentation. Fire still lacks its original cannon and smoke staging.
- The first complete Old 3DS performance/UI pass and the requested live low-level/developed gag
  picker captures remain open.

### Next
- Capture both SOS pages and the live SOS chooser from a full 16-card inventory; tune only from
  native frames and confirm count badges remain legible at two digits.
- Add the bounded temporary NPC actor and stock arrival/departure sequence around the already
  authoritative NPC SOS result.
- Finish Fire staging and run the two-client chat/teleport/treasure latency regression once the
  3DS toolchain, Azahar, and local server are available.

## 2026-10-30 — Informative SOS cards and server-owned SOS playback

### Done
- Reworked the live battle SOS chooser so all 16 touch slots identify what the card actually
  does. Each opaque card now has a gag-track/status colour, NPC name, effect and gag level, plus
  a high-contrast count badge; players no longer have to memorize 41 NPC names before spending a
  limited reward.
- Stopped accepted NPC SOS attacks from disappearing locally after the server spent the card.
  The movie scheduler resolves the friend id through the generated upstream `NPCToons` table and
  plays the corresponding heal/trap/lure/sound/throw/squirt/drop path. Damage, healing and deaths
  still come exclusively from the authoritative `setMovie` arrays.
- Corrected toon attack ordering to match stock `Movie.py`: Fire, SOS, NPC SOS, Pet SOS, then the
  seven ordinary gag tracks. The former numeric sort incorrectly put Fire after every gag.
- Kept unknown and non-gag SOS effects safe: restock, Toons Hit and Cogs Miss retain a bounded
  generic presentation instead of indexing the seven-track gag tables.

### Verified
- Regenerated `source/ui/sos_tables.h` from the pinned Open Toontown checkout; it is byte-stable
  at 41 cards. `python3 -m py_compile` passes for the generator and its boss-bake consumer.
- `git diff --check` passes. The battle host regression now statically asserts that NPC SOS is a
  supported movie track, alongside Fire.
- `tools/battle_fx_test/run_test.sh` was attempted and stopped before compilation because this
  Linux workspace has no `clang++`. The 3DS build remains unavailable without `DEVKITARM`; no
  emulator or hardware result is claimed.

### Asset size decisions
- No assets, allocations or resident textures were added. The chooser reuses immediate geometry
  and the generated 41-entry table already shipped for the SOS book and boss rewards, so the Old
  3DS memory and SD budgets are unchanged.

### Known issues
- NPC SOS now presents its resolved effect and authoritative result, but does not yet instantiate
  the card NPC as a temporary DNA-driven Toon or play the stock teleport-in/greeting/teleport-out
  sequence. Restock, Toons Hit and Cogs Miss still use the bounded generic presentation.
- The denser two-line SOS cards need a native 320 x 240 capture, especially for the longest NPC
  and restock labels, plus touch and D-pad verification on an Old 3DS.
- Fire still needs its original cannon, control and smoke staging. The first complete Old 3DS
  hardware performance and UI pass remains open.

### Next
- Add a bounded temporary NPC actor using the generated SOS DNA table, then reproduce the stock
  teleport/greeting/departure around the resolved gag without growing the Toon cache beyond 4 MiB.
- Capture the SOS chooser, a low-level gag grid and a developed-Toon grid at native resolution;
  tune only from those frames and verify every card sends the selected friend id.
- Finish Fire cannon/smoke staging and run the requested two-client chat/teleport/treasure latency
  regression when a working compiler, Azahar and local server are available.

## 2026-10-30 — Authoritative teammate Laff in Toon-Up targeting

### Done
- Completed the Toon-Up target cards with authoritative teammate health. The battle view now
  carries the local owner's `setHp` / `setMaxHp` values and each remote Toon's already-decoded
  broadcast values; no health is inferred from battle movies or cached damage.
- Added an eight-segment green/amber/red Laff meter and a numeric `current/max Laff` label to each
  named target card. Before both remote fields arrive, the card says `Laff unavailable` instead of
  presenting a false zero.
- Preserved the existing 296 x 38 touch targets, focus ids, Back behavior, and allocation-free
  rendering. The meter uses the post-world-safe rounded triangle primitive rather than procedural
  Citro2D circles.

### Verified
- Traced the values from generated `DistributedToon.setHp` / `setMaxHp` fields through the remote
  Toon decoder and the local owner stats into `Battle::fillView`; the picker consumes only those
  server-owned values.
- `git diff --check` passes. A layout audit confirms the name, 116 px meter, numeric label, and
  focus frame fit inside every target card without changing input geometry.
- This Linux workspace still has no host C++ frontend or devkitARM, so the host suite and `.3dsx`
  could not be built. No emulator or hardware result is claimed.

### Asset size decisions
- No assets, heap allocations, resident textures, or protocol fields were added. The meter is
  immediate geometry and has negligible impact on the Old 3DS budget.

### Known issues
- A live four-Toon battle must confirm that all remote clients receive their HP broadcasts before
  target selection and that long Toon names remain legible at native 320 x 240 resolution.
- The target chooser still needs the requested native capture and a server assertion that choosing
  each card sends that exact teammate's doId.
- Fire/SOS cinematic fidelity, battle-picker hardware performance, and the first complete Old 3DS
  hardware pass remain open.

### Next
- Run the four-client Toon-Up matrix: damage three teammates to different Laff bands, capture all
  cards, cancel once, heal each target, and assert the outgoing doIds.
- Capture low-level and developed-Toon gag grids plus SpeedChat at native resolution, then tune
  typography and focus contrast from the actual frames.
- Finish the original-asset Fire cannon/smoke sequence and NPC SOS arrival/effect playback.

## 2026-10-30 — Cross-play Toon-Up target picker

### Done
- Fixed single-target Toon-Up silently healing the first remote Toon. In battles with two or
  three eligible teammates, choosing a single-target Toon-Up gag now opens a dedicated parchment
  target panel with large named cards; touch or D-pad/A chooses the exact Toon and B/Back cancels.
  One eligible teammate remains immediate, and group Toon-Up gags retain their stock no-target
  behavior.
- Kept the target panel allocation-free and within the existing bottom-screen battle owner. Its
  drawing and focus table share the same geometry, and the host navigation suite now covers every
  card, directional movement, Back, and touch hitboxes.
- Fixed the Cog target frame becoming stranded on a defeated Cog between rounds. The panel now
  selects the first remaining active Cog before drawing or accepting the next attack, keeping the
  highlighted card and Astron request target consistent.
- Restored both pinned shallow reference submodules after finding empty worktrees: Open Toontown
  code at `2b366a2` and resources at `d00df01`. Neither reference checkout was edited.

### Verified
- Compared Toon-Up request semantics against pinned upstream
  `DistributedBattleBase.__handleLocalToonBattleEvent`: single-target HEAL resolves the selected
  active-Toon slot to its doId, while group HEAL carries no individual target.
- `git diff --check` passes, and both reference submodules report their pinned clean revisions.
- `make -j2` reaches the repository's toolchain guard and stops because `DEVKITARM` is unavailable.
  The host has GCC but no C++ frontend; downloading temporary Debian C++ packages was also blocked
  by the environment's package proxy (HTTP 500), so the extended host suite and `.3dsx` could not
  run here. No emulator or hardware result is claimed.

### Asset size decisions
- No new assets, heap allocations, or resident textures. The target cards use the existing
  immediate-mode parchment shapes and cached font; the change is negligible for the Old 3DS
  memory budget.

### Known issues
- The target chooser needs a live four-Toon battle capture at 320 x 240 and a server assertion
  that each of the three cards produces the chosen teammate's doId.
- Battle participant views do not currently expose remote Laff values, so the cards can identify
  teammates by name but cannot yet show their health. Add the owner-visible health field only if
  the stock protocol supplies it; do not estimate it client-side.
- The broader existing issues remain open: Fire/SOS cinematic fidelity, native battle-picker
  readability/performance, and the first complete Old 3DS hardware run.

### Next
- Run a four-client battle with a single-target Toon-Up gag, capture all three cards, cancel once,
  then heal each teammate and assert the outgoing target doIds in the timestamped network log.
- Capture the low-level and developed-Toon gag grids requested by the visual review, then tune
  native-size typography and focus contrast from actual 320 x 240 frames.
- Finish Fire cannon/smoke staging and NPC SOS arrival/effect playback using original assets.

## 2026-10-30 — Readable locked-gag ladders and battle actions

### Done
- Replaced the live battle picker's anonymous dark `?` pills with full parchment cards. Every
  locked level now retains its position, track-colour ribbon and level number, so a new Toon can
  understand the seven-level progression without confusing an unavailable gag for an empty slot.
- Applied the same card treatment to the Shticker Book Gags page. Unearned levels no longer
  collapse into six-pixel dots beside the original gag art; the book and battle picker now use
  the same visual language at native bottom-screen scale.
- Gave Fire, SOS, Pass and Run distinct muted parchment colours when available. Disabled actions
  remain dark and readable, and drawing still uses the existing rounded triangle primitive that
  survives the post-world PICA state.
- Preserved every existing touch rectangle and focus id, so this polish does not alter battle or
  book input semantics.

### Verified
- `git diff --check` passes for both UI commits and this progress entry.
- Source/layout audit confirms all seven gag columns still fit before the XP bar, and the battle
  grid and four action controls still draw inside their shared `pad_tables.h` focus rectangles.
- A full `make -j2` was attempted and stopped at the Makefile's environment guard because this
  Linux workspace has no `DEVKITARM`. No emulator or hardware result is claimed.

### Asset size decisions
- No new assets or allocations. The pass reuses the original gag atlas and draws small solid
  cards, ribbons and numerals; it does not increase the SD set, texture residency or load time.

### Known issues
- The new locked-card contrast and special-action colours still need a native 320 x 240 capture
  in Old 3DS mode and a real-hardware readability/touch pass.
- Fire still lacks the stock cannon/smoke/launch geometry, and NPC SOS still lacks its faithful
  temporary-Toon arrival and resolved gag/heal movie.
- The broader existing visual and gameplay issues in older entries remain open, including the
  first complete real-hardware run.

### Next
- Run `AUTOTEST_FROM="see cog" AUTOTEST_ONLY="see cog,pad: battle"` with autoplay disabled and
  capture both a low-level Toon (many locked cards) and a developed Toon at native resolution.
- Capture the Shticker Book Gags page from the same profiles, checking two-digit badges, XP text,
  focus contrast and the boundary between the seventh card and XP bar.
- Complete the original-asset Fire and SOS movie fidelity pass, then measure the picker cost and
  memory on Old 3DS hardware.

## 2026-10-29 — Fire movie playback and cross-play chat diagnostics

### Done
- Fixed the battle movie scheduler silently discarding stock Fire attacks (track 100). A Pink
  Slip accepted by the server now produces an 8.5-second dismissal sequence, drives the Toon's
  button animation, applies the server-provided Cog hit/death events, and plays the original
  baked `SA_pink_slip` sound.
- Kept special tracks out of the seven-track gag tables and held-prop pool. Fire previously
  would have occupied a scarce effect slot with no drawable model; normal gag props retain all
  available slots.
- Added named constants for all stock special battle tracks and regression assertions covering
  Fire movie admission and timing. NPC SOS remains deliberately excluded until its NPC lookup,
  teleport, and resulting gag/heal movie can be implemented faithfully.
- Added allocation-free, monotonic timestamped chat diagnostics: incoming/outgoing SpeedChat and
  incoming typed chat now produce stable `net: chat rx/tx ...` lines for cross-play assertions.

### Verified
- `git diff --check` passes for both implementation commits and this progress entry.
- Source audit confirms `TCP_NODELAY`, nonblocking sockets, and the dedicated receive/send thread
  were already present; chat logging adds no work to the per-frame idle path.
- The battle host regression was extended, but could not execute because this Linux workspace has
  no `clang++`, `g++`, devkitARM, or Azahar. No runtime result is claimed.
- The pinned reference checkout was attempted as requested. The 668 MB resources submodule could
  not fit in the 215 MB free workspace volume; the failed empty checkout metadata was removed and
  no upstream reference file was modified.

### Asset size decisions
- No new assets. Fire streams the original `SA_pink_slip.tsnd` already emitted by
  `bake_audio.py`; the change adds no resident texture, model, or audio allocation.

### Known issues
- Fire does not yet stage the stock cannon, smoke, and launch geometry; it now presents the
  accepted attack instead of skipping it, with correct sound and server-owned defeat outcome,
  but still needs that visual-fidelity pass.
- NPC SOS attacks are still skipped by the movie scheduler. The picker and protocol request work,
  but a faithful movie needs the NPC-id table, temporary Toon actor, teleport, and the card's
  resolved gag/heal effect.
- Fire/SOS inventory decrement and presentation need a live-server Azahar capture. Old 3DS timing,
  readability, and memory remain unmeasured in this workspace.

### Next
- Bake the original Fire cannon/control/smoke assets, reproduce `MovieFire` staging within the
  battle effect budget, and capture the dismissal at native 400 x 240.
- Resolve an NPC SOS card id through the stock NPC table and play its teleport plus resolved
  seven-track effect, then assert the owner inventory decrement.
- Run a two-client chat/teleport/treasure regression and compute web-to-3DS median latency from the
  new `net: chat` timestamps; keep the emulator target under 45 ms.

## 2026-10-29 — Stock-compatible Fire and SOS battle choices

### Done
- Completed the live battle picker's requested Fire / SOS / Pass / Run action row. Fire is
  enabled only when the owner-view `setPinkSlips` count is nonzero, displays that count, targets
  the selected active Cog, and sends stock `requestAttack(100, -1, cogDoId)`.
- Added a real SOS ToonFriend chooser backed by the owner's `setNPCFriendsDict`: up to 16 owned
  cards appear as opaque rounded cards in two columns with original NPC names and counts. Touch
  and D-pad/A select a card, B or Back returns without spending one, and selection sends stock
  `requestAttack(97, -1, npcId)`.
- Passed the already-authoritative Pink Slip and SOS inventories into street, facility, and boss
  battle panels. Disabled actions remain visible so the bottom row is stable and understandable.
- Extended the shared battle focus table and host regression source for the new touch rectangles.
  SOS focus starts on the first owned card, including when earlier inventory slots are empty.

### Verified
- `git diff --check` passes.
- Source audit confirms the special tracks and targets match `docs/design/combat.md` and the
  existing generated `requestAttack(i8,i8,i32)` field.
- A full build was attempted, but this workspace has neither `DEVKITARM` nor a host C++ standard
  library. The host runner and 3DS build therefore could not execute here; no test result is
  claimed from the failed environment checks.

### Asset size decisions
- None. The chooser reuses generated SOS names and immediate-mode rounded UI geometry; it adds no
  textures, models, audio, or runtime allocation.

### Known issues
- Fire and SOS selection need a live-server Azahar pass to confirm inventory decrements and movie
  presentation after the server accepts each request.
- The existing battle movie has generic SOS presentation and no dedicated pink-slip firing
  animation; protocol selection is now correct, but those cinematic fidelity gaps remain.
- Real-hardware readability and touch comfort still need measurement on an Old 3DS.

### Next
- Give a test toon Pink Slips and at least two SOS cards, capture both picker states at 320 x 240,
  and assert `net: battle ... requestAttack track 100/97` plus the server-owned count decrement.
- Add the stock Fire dismissal and SOS arrival/effect movies, preserving the existing Old 3DS
  animation and memory budgets.

## 2026-10-29 — Toontown UI art direction pass

### Done
- Moved the real species- and head-colour-aware Laff meter from the top-screen world view to
  the bottom-screen Map. It now follows the original lower-left placement; Home moved beside
  it while retaining its 76 x 24 touch target and the playground teleport row remains full
  width. Healing/damage pulse, teeth, eyes and both Laff numbers are preserved.
- Reworked the live battle picker from sparse debug rows into a stable 7 x 7 gag grid with
  original gag icons, paper slots, opaque count badges, visible locked slots, a strong focus
  frame and rounded Cog cards. Cog health is now a five-lamp green/amber/red chest-style meter
  instead of a flat grey progress slab. The round timer and Pass/Run controls have framed,
  readable states.
- SpeedChat phrase and category rows are now opaque rounded cards with category colouring,
  outlines and an explicit focus state. This prevents the menu from reading as loose text over
  the map and keeps touch/D-pad behaviour unchanged.
- Replaced the front end's generic navy square buttons with a shared rounded speech-card
  primitive. Title, Pick-a-Toon, Make-a-Toon, name shop, Toontorial choice and error screens
  now use sky, parchment, red-leather and gold colours with dark readable labels. Button
  geometry and hitboxes did not change.
- Enlarged the Shticker Book Gags page from loose 15 px icons to framed 18 px slots. Every
  learned gag has an opaque track-coloured count badge, including zero counts, so numbers no
  longer disappear into the icon art. XP bars were narrowed slightly without clipping labels.
- Removed the last player-facing `C2D_DrawCircleSolid` calls (locked Gag slots and SOS rating
  marks). Those procedural circles are known to lose PICA proctex state after the world shader;
  both pages now use the triangle-based rounded primitive shared by SpeedChat and battle UI.

### Verified
- `git diff --check` passes after both new commits.
- Source audit finds no remaining `C2D_DrawCircleSolid` calls in `source/ui`; the one renderer
  reference is the existing explanatory comment for map markers.
- Layout constants for the shifted Home control are shared by drawing, touch handling and the
  focus table, with compile-time assertions covering x/y/width/height. The host pad-navigation
  expectation was updated for its new nearest playground column.

### Asset size decisions
- None. This pass reuses the existing original gag and Laff-meter atlases and adds no runtime
  assets. The rounded shapes are small immediate-mode triangles.

### Known issues
- This workspace has no host C++ compiler, devkitARM/libctru, Azahar or hardware connection.
  The `.3dsx`, host suites and requested live battle/SpeedChat screenshots could not be run
  here; this pass is source- and layout-verified only.
- Fire and SOS choices are not wired into the battle protocol yet; the current live picker
  exposes the seven normal gag tracks, Pass and Run.
- Real-hardware readability, touch comfort and the cost of the additional rounded-card
  triangles still need measurement on an Old 3DS.
- The existing emulator/gameplay issues in older entries remain open.

### Next
- Build and run `AUTOTEST_FROM="see cog" AUTOTEST_ONLY="see cog,pad: battle"` with autoplay
  off, then capture the selectable gag picker and SpeedChat at native 3DS resolution.
- Add stock-compatible Fire and SOS selection after confirming their exact Open Toontown DC
  request layouts and inventory semantics; do not expose decorative buttons before they work.
- Run the front-end flow and every Shticker Book page in Old 3DS mode, checking contrast,
  count badges, focus rings, pulse clipping and touch edges.

## 2026-10-28 — Player-facing bottom screen and hidden diagnostics

### Done
- The bottom screen now starts on the Map instead of the performance Debug panel.
- Removed Debug from the touch tab bar. The five player tabs (Map, Chat, Toon, Gags and
  Setup) use the full width; SELECT opens the hidden performance view and returns to the
  exact player tab that was previously open. D-left/right also leave Debug safely.
- Replaced the visible Setup tab's developer-only stress-Toon controls with a large
  Shticker Book button. Stereo 3D remains available there; detailed music, sound, graphics
  and development controls remain in the book's Options page.
- Updated the Setup focus table and its host regression expectations so touch and D-pad
  activation share the same full-width button rectangles.
- Fixed a shared text-cache ownership bug: changing quickly between Gags and Debug could
  show the other panel's cached rows for up to ten frames. Each panel now invalidates the
  cache when it becomes the owner.
- Updated the README controls for START, SELECT and START+SELECT.

### Verified
- `git diff --check` passes after each commit.
- Cross-reference audit finds no stale Setup-tab stress-control identifiers; the remaining
  stress controls intentionally belong to the Shticker Book Options page and autotest.
- The host pad-navigation test was updated for the new Setup layout, but could not execute:
  this workspace has no `clang++`, `g++`/`cc1plus`, or devkitARM toolchain.

### Asset size decisions
- None. This round changes UI state, layout and input only.

### Known issues
- The new five-tab layout, hidden Debug transition and Setup copy still need a native-size
  visual pass in Azahar or on hardware.
- The existing emulator/hardware and gameplay issues below remain open.

### Next
- Build with devkitARM and run the bottom-screen scripted pass in Old 3DS mode, checking the
  five 64 px tabs, SELECT round-trip, Setup focus ring and Gags/Debug rapid switching.
- Continue the real-hardware readability and touch-target pass across every Shticker Book
  page.

## 2026-10-28 — Shticker Book touch and button-input repair

### Done
- Fixed the Cogs and Cog Disguises department tabs, Fish Album genus arrows, and SOS
  ToonFriends page control to use their shared screen-space focus rectangles. Their old manual
  touch tests used page-local Y coordinates even though the page is drawn below the 24 px book
  cover bar, leaving controls partly or wholly untappable.
- Those controls now have one activation path for touch and A, so the visible D-pad focus ring
  always activates the highlighted control. D-left/right shortcuts remain unchanged.
- Book input now receives the live `OwnBookStats` pointer before dispatch. SOS pagination can
  therefore see that the Toon owns more than eight cards; it previously always saw an empty
  input context and rejected page changes even while drawing both pages.
- Extended `padnav_test` with exact hitbox and A-activation checks for all repaired controls.

### Verified
- `padnav_test`: 128 checks, 0 failures (GCC 14 host build with ASan/UBSan).
- `book_test`: all experience, disguise, golf trophy, SOS table, and quest-text checks pass.
- `git diff --check` passes.

### Asset size decisions
- None. This round changes input behavior and tests only.

### Known issues
- The existing emulator/hardware and gameplay issues below remain open.
- This workspace does not contain devkitARM/libctru, so the `.3dsx` could not be rebuilt or the
  repaired controls visually exercised in Azahar here. The affected shared focus model and
  book data suites were host-tested instead.

### Next
- Run the book pages in Azahar or on hardware and visually verify touch at the edges of each
  control, focus rings, and SOS inventories of 9-16 cards.
- Continue the first real-hardware pass, especially bottom-screen text readability and touch
  target comfort at native 3DS size.
## 2026-10-29 — Classic SpeedChat and isolated Linux UI captures

### Done
- Replaced the bottom screen's dark, square SpeedChat list with opaque cream-white rounded
  speech cards, category-coloured outlines, a sky-blue header, dark readable text and a gold
  D-pad focus state. Submenus retain their root category colour. The rounded geometry uses
  rectangles and triangles rather than Citro2D's procedural circles, which are unreliable
  after the world pass.
- Added a reusable `ui/rounded_rect.h` primitive for subsequent front-end, battle and book
  polish. It adds no textures or runtime allocations.
- The autotest now captures `shot_speedchat_*` directly, and X opens Chat deterministically
  even when a focused test starts in isolation.
- `azahar_test.sh` no longer globally kills Azahar or shares mutable emulator state on Linux.
  Each run gets its own config, log and SDMC tree, symlinks the large read-only data/NAND
  inputs, and terminates only its own process group. Raw framebuffer dumps are preserved even
  when the optional Panda3D decoder environment is unavailable.

### Verified
- devkitARM/libctru full build: `toontown3ds.3dsx` produced successfully.
- Azahar 2126.2, Old 3DS mode, Linux/Xvfb: focused `pad: tab chat` capture shows all visible
  root labels and arrows over opaque rounded cards at 320x240; no clipping and zero emulator
  panics. The visual pass caught and fixed an initial card/text depth-order regression.
- The isolated test ran concurrently with an existing Azahar process without stopping it or
  altering its trigger/config tree. The UI profiler was about 1.1 ms on this software-GL rig.

### Asset size decisions
- No new bitmap assets. SpeedChat's rounded panels are 2D solid geometry, avoiding both a UI
  atlas addition and the post-world procedural-circle issue.

### Known issues
- The focused capture covers the root SpeedChat menu and D-pad highlight path; every nested
  menu still needs a complete human touch/scroll review on hardware.
- macOS still uses Azahar's standard config/data locations (but no longer globally `pkill`s
  emulator processes); per-run macOS profile isolation remains to be implemented and tested.
- No real-hardware run yet. The larger project issues below (boss coverage, street/facility
  load hitches, golf branch, and remaining UI/visual review items) remain open.

### Next
- Apply the rounded speech-card treatment to the front-end buttons and Toontown-style battle
  controls, then capture the live selectable gag picker.
- Move the species-correct Laff meter to the bottom-screen layout without covering touch
  targets, and complete a real Old 3DS performance/readability pass.

## 2026-10-27 (rounds 4-5) — DA office and country club won, racing speed, reconnect fixes

### Done
- **Reconnect never rejoins a dead dynamic zone** (round 4). A drop on a facility floor (or
  in a minigame, race, golf or estate zone, all >= 61000) reconnected into that zone after
  the AI had freed it: an empty floor with no Cogs, no elevator and no way out. Last round's
  "DA office floor elevator has no position" was exactly this. The session now returns to the
  last static zone (the facility lobby; a street cell becomes its playground) and the world
  follows with a toast. nettest has a dynamic-zone drop case.
- **Arrivals settle on the new place's floor** (round 4): winning the DA office dropped the
  toon at Lawbot HQ 68 units in the air (the reset used the facility's collision).
- **Karts were held to ~20 units/s.** `Racing::update` took a flat 10% off the velocity on
  every frame the kart touched a wall. Panda's `PhysicsCollisionHandler.apply_net_shove`
  (read from the Panda source) only counts a kart moving *into* the wall, removes that
  component, and applies friction 0.1 x cos(velocity, wall normal). On Screwball Stadium the
  kart slides along the barrier for most of a lap, so it never finished one and the AI kicked
  it as too slow. The rule is now `racing::Vehicle::wallShove`, shared by the runtime and the
  host drive test (which had no wall response at all, so it never saw this); new head-on /
  grazing / moving-away vectors. Host: all 12 tracks run 3 laps, Screwball in 129 s.
- **A drop mid-race got the next session ejected.** `resetWorld` cleared every object family
  except racing and golf, so after a reconnect Racing rebuilt itself from the dead race and
  acked its barrier; Astron ejected the new session (117, update to a nonexistent object).
  Both are now cleared on disconnect; `racing_test/client` checks it (and fails without it).
- `azahar_test.sh`: runs really are muted now (Azahar ignores a value while its `\default` flag
  is true), headless Linux uses the Null audio sink, and the kill pattern no longer leaves an
  Xvfb server behind per run (51 had piled up on this box). The VP/CFO `rounds` steps re-board
  from the lobby after a reconnect instead of idling.
- autotest: the facility `next floor` step rides on to the top floor on purpose (floor
  elevators are never locked, `DistributedClubElevatorAI.isLocked = 0`) and stops on the floor
  with the boss Cog.

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live server, `AI_DEV_STRONG`)
- **DA office won end to end** (round 4): floor 1 (lasers, security camera, room battle),
  floor elevator, floor 2, boss battle, `StageReward`, back in Lawbot HQ. 26-30 fps.
- **Country club won end to end**: kart boarding, floor 1 (maze, mole field, golf green,
  room battle), floor elevators through floors 2-4 to floor 5, the president-room boss
  battle, `CountryClubReward`, `level: 10600 defeated -> zone 10000`. No PARSE/EJECT lines.
  ~30 fps, up to 177 draws / 14.2k tris, heap 3.9 MB, linear free >= 11 MB. A floor change
  costs a 0.2 s + 0.4 s main-thread hitch (floor `.tlf` and room loads), behind the elevator
  panel.
- **Cogdominium won end to end** (first run on this rig): field office boarding, the maze
  (14 Cogs, 9 coolers, gags thrown, exit), the executive-suite battle, `BuildingReward`,
  `victor ready`, the building back to a toon building. 30 fps, 57 draws, 9.3k tris.
- **Speedway Practice race end to end** (first time on any rig): owned kart, Practice
  block, barriers, three laps on Screwball Stadium (~42 s a lap after the fix), the
  authoritative result, back in the Speedway with 20 tickets. 29.9 fps (min 25), <= 55 draws.
- Host: racing_test (all parts), nettest (all, including both reconnect cases).

### Asset size decisions
- None this round.

### Known issues
- **The Azahar emulated-clock stall still ejects runs here (345)**, and it now blocks the boss
  battles entirely. Six more this round: Cogdo after its win, the Speedway twice, and all
  three Sellbot VP attempts, each within seconds of entering the lobby or the boss office
  (one froze ~13 min of wall time over ~5 s of emulated time, during idle 30 fps frames). The
  client keeps sending heartbeats in emulated time (outgoing bytes rise by exactly one 4-byte
  frame per 10 s). Ruled out: CPU starvation (load < 3.5 on 8 cores while Azahar spins) and
  the failed audio sink (the Null sink stalls the same way). Still unseen on the Mac rig.
  **The four boss battles therefore remain unverified in an emulator**; they need the Mac rig
  or hardware.
- A facility floor change freezes the main thread 0.2-0.4 s (behind the elevator panel).
- The tailor keeps the clerk camera (the counter is baked into the interior mesh).
- Street loads take 1.0-1.3 s on the main thread (behind the teleport/door fade).
- Not run on this rig yet: the four boss battles (VP, CFO, CJ, CEO), the estate, golf
  (unmerged `golf2`), interior-life steps. No real-hardware run yet.
- Never relink while Azahar runs the tree's `.3dsx`; test from a copy.

### Next
- The four boss chunks (`boss: board` onward) on the Mac rig or hardware; this rig can't hold
  them.
- Move facility floor loads off the main thread (the shared loader already handles props).
- First real-hardware run (frame budget, the socket stall question).

## 2026-10-27 (later) — SD set halved, Cog buildings winnable, battle hitches, clerk camera

### Done
- **SD data set 992 MB -> 603 MB** (586 MB apparent), with no change to any asset's quality:
  - Zone `.tz` files copied every atlas page into every chunk that used it (near and far):
    271 MB of texture copies for ~12 MB of distinct payloads. **TZ v3** stores each payload
    once in a per-file pool; chunk TTMs point into it (`Model::loadFrom` unchanged). zones
    389 -> 131 MB (2100.tz 22.0 -> 6.2 MB), level 68 -> 58 MB. `tz.py <files>` converts
    existing bakes losslessly (v3 -> v2 gives back the original bytes); bakers write v3.
  - The 538 interiors held 120 MB of copies of ~2 MB of textures. `ttm_pool.py` writes one
    `interiors/textures.ttp` and pooled TTM v2 models (170 -> 42 MB); deterministic,
    idempotent, `--unpool` restores the exact v1 bytes; `bake_sd_all.sh` runs it.
- **Cog buildings could not be won.** Three bugs, found by the first strong-toon run:
  - The interior elevator was never found: `DistributedElevatorIntAI`'s `bldgDoId` is the
    interior's doId, not the exterior building's. Toons were stranded on floor 1.
  - Boarding was edge-triggered on entering the sphere, so a toon already standing in it
    when the doors opened never boarded. It also accepted the `opening` state, which
    upstream never does: boarding then wedged the AI (`no transition from opening to
    allAboard`) and the floor never started.
  - After the top floor's reward the toon stayed inside forever: `extZoneId` was parsed
    but unused. `DistributedSuitInterior.enterReward` now takes the toon back to the street
    at the building's elevator, and `setVictorReady` is sent once the building is in
    `waitForVictors`, so the AI turns it back into a toon building.
- **Battle freezes removed.** The battle face-off froze ~140 ms (attack props loaded on
  the main thread) and each Cog death ~280 ms (lose model and clip loaded synchronously).
  Both now stream through the shared background loader, the props at battle start and the
  lose model when a battle first places the Cog.
- **Clerk camera.** The Gag Shop, tailor, pet, kart, fisherman and party clerks now use
  upstream's purchase-movie camera (1 s ease to the clerk-relative over-the-shoulder view).
  The autotest drivers walk up to clerks like a player instead of teleporting inside
  the counter.
- Cog building and Cog HQ shells load only on streets (they were loaded synchronously in
  every playground, interior and HQ: ~250 ms and ~350 KB linear per load).
- `battle_validate` counts the clothing a battle really holds (it counted the whole 1 MB
  `clothes.ttx`, which is streamed) and passes with the full SD set.
- A 10 s `net: link` log line (bytes in/out, heartbeat age, send backlog) for diagnosing
  disconnects.

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live server with `AI_DEV_STRONG`)
- v3 zones 2000/1000/5000/3000 and pooled interiors 2519/2521/2522/8501 load and render
  correctly (screenshots); zone_test (the real C++ loader on the host) passes for v2 and v3,
  with new corruption cases for the pool.
- Factory (steps 147-162) to the foreman, won, back to Sellbot HQ: 26-30 fps.
- Cog building (3 floors) won end to end: floors 1-3, elevator rides, reward, street,
  `victor ready`. No frame hitch in any of the battles.
- Gag Shop, tailor and kart shop camera framing (screenshots).
- Cashbot mint (`go mint` .. `mint: boss`): boarded, walked every room and hallway, room
  battle, boss battle won (`level: 12500 defeated -> zone 12000`), back in Cashbot HQ.
  29.9 fps in the mint; heap 3.6 MB, linear free >= 9.5 MB.
- DA office, floor 1: boarded, walked, laser field hits and hazard, security camera, room
  battle. 30 fps, 87 draws, 10.4k tris, linear free 9.2 MB. The floor elevator was never
  found (see Known issues).

### Asset size decisions
- None downscaled. The size cut is pure de-duplication; every texture keeps its baked
  resolution and format.

### Known issues
- The tailor's upstream toon-facing clothes camera needs its counter hidden; the counter is
  baked into the interior mesh, so the tailor keeps the clerk view.
- Street loads take 1.0-1.3 s on the main thread; the teleport/door fade covers them.
- **Six runs on this rig were ejected for heartbeat timeouts (345)**, in Cog buildings and
  facilities. The `net: link` line shows the client queueing a heartbeat every 10 s of
  emulated time right up to the drop; the log clock shows Azahar's emulated time stopping
  for 5-10 minutes and resuming exactly when Astron closes the socket, even with the
  timeout raised to 300 s (`ASTRON_HEARTBEAT_TIMEOUT_MS`, new, test rigs only). Facilities
  also average only ~35-50% emulation speed here. The stall looks like Azahar's socket
  emulation (software GL, Xvfb, no audio device); it has not been seen on the Mac rig and
  needs a hardware run to rule out the client. Fps/ms numbers here are emulated time.
- **DA office floor elevator**: on floor 1 `level.elevatorPosition` never returns a position,
  so the next-floor step cannot board (AGENTS.md: stage entry was already listed as open).
  The country club has the same floor-elevator path and is untested.
- The net loop now calls `recv()` only when `poll()` reports the socket readable (it ran on
  every ~10 ms idle iteration, a SOC call each on hardware).
- Still not run on this rig: DA office floors 2+ and its boss, country club, speedway, Cogdo, the four
  bosses, interior-life steps. No real-hardware run yet.
- Never relink while Azahar runs the tree's `.3dsx` (RomFS is read from it); test from a copy.

### Next
- Find why the DA office floor elevator has no position, then the club and boss chunks.
- Pin down the Azahar socket stall (try `graphics_api` software / Vulkan, an audio sink,
  or a hardware run).
- Shared textures in residency too (one upload per zone texture instead of per chunk).
- First real-hardware run.

## 2026-10-27 — Linux test rig, front-end polish, Toontorial end to end

### Done
- **Everything now builds and runs on a plain Linux x86_64 box** (no sudo, no Docker daemon):
  - devkitARM / libctru / citro3d / citro2d: the layers of the `devkitpro/devkitarm` Docker
    Hub image, fetched with curl and unpacked (pkg.devkitpro.org answers 403 here).
  - Azahar 2126.2 Linux AppImage, extracted and run under Xvfb (OpenGL, software GL).
    `tools/dev/azahar_test.sh` now works on Linux as well as macOS.
  - The real Open Toontown server: `tools/server/setup_linux.sh` (micromamba conda-forge
    toolchain, astrond built from Astron, the Panda3D fork built at `setup.sh`'s pinned
    commit). `start.sh`/`stop.sh` are portable. `smoke_test.py` passes.
  - The full SD data set: `tools/pipeline/bake_sd_all.sh` (6 parallel lanes, 16m47s on 8
    cores, 992 MB). Bakers that also write committed `romfs/`/`source/`/`docs/` outputs run
    with those outputs redirected to scratch, so a bake never dirties the tree.
  - Host test suites build with GCC 12 (x86); they were previously only run with Apple clang.
- **Front end**: the title shows the original `phase_3/maps/toontown-logo` at the
  proportions of `progress-background`'s logo card (new `bake_title_logo.py`; RGBA8
  256×128, 128 KB RomFS). The bottom screen names the configured `host:port`. Before any
  session exists, Backoff says "Can't reach the server. Trying again..." instead of
  "Connection lost". Centred labels shrink to fit; the Toontorial prompt no longer runs off
  both screen edges.
- **Debug tab**: every row fits the 320 px screen (the net row and the log line overflowed).
- **Toontorial bugs (found by the first live run on this rig)**:
  - After beating the Flunky the game was stuck on the reward forever: the battle panel
    consumes A/Continue, and Continue was ignored in the tutorial while the main-loop hook
    waited for an A it never saw. Continue is now the acknowledgement.
  - The tutorial Flunky id was never cleared when the AI deleted it, so the tutorial never
    advanced to HQ Harry. It is cleared on OBJECT_LEAVING.
  - The autotest driver teleported onto flush door/tunnel triggers and stood still; a
    walking toon's probe touches them, a standing one is pushed back just out of reach.
    The driver now runs into the HQ back door and the TTC tunnel like a player.
- **x86 portability bug**: the HQ leaderboard pickle decoder compared a `uint8_t` with
  `'\x85'`..`'\x87'` (negative on x86, so TUPLE1–3 never matched in host tests).
- Host tests no longer depend on one Mac's absolute paths (cog thief, target, suit refs).
- **Map tab indoors** showed a meaningless grass-green square; interiors now get a plain
  floor-plan panel labelled "Indoors".
- **Facility elevators missing from a fresh SD bake**: `bake_sd_all.sh` now runs
  `coghq_elevators.py`, without which the factory/mint/DA office elevators had no anchors
  (not placed, not boardable).

### Verified (Azahar 2126.2, Old 3DS mode, Linux/Xvfb, live local server)
- Title → connect → Pick-a-Toon → Make-a-Toon (all 5 steps, Pick-a-Name and typed name)
  → create → TTC: 29.9 fps, heap 3.5 MB, linear free 15.4 MB, VRAM free 4.2 MB.
- Toontorial: Tom's interior, Mickey's intro, Tom's quest, the street, the Flunky battle
  (throw + squirt), reward, HQ Harry, out of the HQ. See test-log for the tunnel result.
- Offline world script (moves, tabs, padnav, book steps): 27–30 fps, 172 draws, 13.3k tris.
- Online world script, steps 0–163, in three chunks (`AI_MINIGAME_ID=8`): playground,
  clips/emotes, treasure, Mickey, fishing, trolley + Catching game + gag purchase, door,
  Gag Shop/pet/kart/fisherman/party clerks, tunnel, street, street battle (all movie
  tracks), teleports, DD boat and swimming, DG, BR, all four Cog HQs, Toon HQ quests,
  tailor, and the Sellbot factory (board, button, stomper, lift, barrel, gear, mixer,
  conveyor, door, crates, goons, room battle). No PARSE/EJECT/crash lines.
- Host suites: 30 of 32 pass on x86. `golf_test` needs golf data from the unmerged `golf2`
  branch; `cog_thief_test` passes after the path fix.

### Asset size decisions
- Title logo: the 256×256 upstream texture is shown at 205×72 px, so it is stored
  256×128 RGBA8 (RGBA4 banded the soft outline). No other asset was downscaled this round.

### Known issues
- The SD data set bakes to 992 MB, about twice the 480 MB the README states; `zones/`
  alone is 389 MB (37 `.tz`, some over 11 MB). Needs a size review before shipping SD packs.
- `battle_validate` fails its 4 MiB battle-scene budget when the full `clothes.ttx`
  (about 1 MB) is installed instead of the Make-A-Toon subset.
- Golf is not merged (`golf2` branch); `golf_test` cannot run without its data.
- No real-hardware run yet; all frame numbers are emulator numbers.
- Azahar on Linux runs here without audio (no DSP firmware, no sound device).
- The autotest presses A every frame in the tutorial, so the Flunky reward panel is only
  shown for one frame in automated runs; a human sees it until they press Continue.

- The scripted test toon is a brand-new 15-laff toon with no gags bought, so it goes sad
  in the street battle and the factory room battle; the foreman, Cog building, mint, DA
  office, club, boss and minigame passes after step 163 were not run this round.
- Factory frames dip to 21–27 fps in Azahar while screenshots are captured (shot I/O
  4–15 ms/frame); run with `shots off` for frame-rate numbers.

### Next
- Give the world script a stronger toon (or dev Cogs) and run steps 164+ (Cog building,
  mint, DA office, club, bosses, estate, golf, racing) on this rig.
- Review the zone `.tz` sizes against the SD budget.
- First real-hardware run.
