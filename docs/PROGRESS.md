# Progress log

Dated entries: what was done, how it was verified, and what is next. Test numbers live in
`docs/test-log.md`; architecture and decisions in `AGENTS.md`.

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
