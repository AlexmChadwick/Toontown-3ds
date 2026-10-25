# Cog Attack Movies Specification (`MovieSuitAttacks`)

## 1. Overview

In Toontown combat, each Cog round executes Cog attacks chosen by the AI server.
The server sends `DistributedBattle.setMovie` containing `BattleSuitAttack` structures.
Each suit attack specifies:
- `suitIdx`: 0..3 index of the attacking Cog
- `attack`: 0..5 index into the Cog species' attack table (`SuitAttributes[cog]['attacks']`)
- `target`: target toon slot (-1 for group attacks, 0..3 for single target)
- `hp`: array of damage dealt per toon slot (0 = miss / dodge, >0 = damage hit)

Rather than playing a generic 2200 ms slot with a generic `throw-paper` clip and cringe,
the native 3DS client implements the full upstream `MovieSuitAttacks` specification with table-driven
timing, props, projectile paths, animations, sounds, and toon reactions.

## 2. Upstream Architecture & 3DS Implementation

### 2.1 Attack Table & Dispatch
There are **67 total attacks** defined in `SuitBattleGlobals.SuitAttacks`.
- **63 attacks** are actively used by the 32 Cog species on stock Open Toontown.
- **4 attacks** are unused by any stock Cog: `FiveOClockShadow`, `Gavel`, `ReOrg`, `SandTrap`.
- **7 attacks** map to `doDefault` in upstream `MovieSuitAttacks.py`: `CigarSmoke`, `FiveOClockShadow`,
  `FloodTheMarket`, `Gavel`, `SandTrap`, `SongAndDance`, `ThrowBook`.

### 2.2 Body Timings
Suit animations run at 24 fps in Panda3D and are baked at 12 fps for 3DS morph skinning.
Attacks with body-specific releases (`suitType in ('a', 'b', 'c')`) have per-body durations and delays
stored in `CogAttackInfo.durationMs[3]`. The runtime selects the duration based on `body % 3`.

### 2.3 Props & Trajectories
Cog attack props are baked into `.ttm` static meshes or `.ttm` + `.tta` actors with ETC1 textures.
- Held props attach to the Cog's `rightHand` or `leftHand` joint.
- Thrown projectiles follow a parabolic trajectory computed by `ProjectileArc` from the hand release point
  to the target toon's chest / face point.

### 2.4 Sounds
Attack sound effects (`SA_*.ogg`) are baked to DSP-ADPCM `.tsnd` files on SD and played via `BattleFx::playCue`.

### 2.5 3DS Particle Simplifications
Panda3D `.ptf` particle systems with runtime physics are simplified on the 3DS GPU to respect the 30 fps locked budget:
- **Impact bursts & splashes** (`FountainPen`, `Demotion`, `Watercooler`, `Liquidate`): rendered using compact
  quad splat/splash cards (`splat.ttm`, `splash.ttm`) with depth bias.
- **Floating text / letters** (`BuzzWord`, `DoubleTalk`, `Filibuster`, `Jargon`, `Legalese`, `MumboJumbo`):
  omitted on Old 3DS to avoid draw-call overhead; the animated prop, head glow, sound effect, and toon reaction carry the comedic action.
- **Clouds & props** (`BrainStorm` stormcloud, `Canned`, `RedTape` tubes, `Sacked`): rendered as baked 3D meshes.

## 3. Complete Attack Reference (All 67 Attacks)

| ID | Attack Name | Cog Anim | Group | Dur A (ms) | Dur B (ms) | Dur C (ms) | Hit Delay (ms) | Toon Hit | Toon Miss | Prop | Sound | 3DS Treatment |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | **Audit** | `phone` | No | 7408 | 7408 | 7408 | 3210 | `conked` | `duck` | `None` | `SA_audit` | Particle omitted |
| 1 | **Bite** | `throw-paper` | No | 7538 | 7538 | 7538 | 5600 | `cringe` | `cringe` | `teeth` | `None` | Baked mesh |
| 2 | **BounceCheck** | `throw-paper` | No | 9308 | 8608 | 8608 | 5110 | `conked` | `sidestep-left` | `bounced-check` | `SA_pink_slip` | Baked mesh |
| 3 | **BrainStorm** | `effort` | No | 7460 | 7460 | 7460 | 4510 | `cringe` | `sidestep-left` | `stormcloud` | `SA_brainstorm` | Baked mesh |
| 4 | **BuzzWord** | `speak` | No | 7042 | 7042 | 7042 | 4510 | `cringe` | `duck` | `None` | `SA_buzz_word` | Particle omitted |
| 5 | **Calculate** | `phone` | No | 7408 | 7408 | 7408 | 3210 | `conked` | `sidestep-left` | `None` | `None` | Particle omitted |
| 6 | **Canned** | `throw-paper` | No | 7682 | 7682 | 6982 | 5530 | `struggle` | `sidestep-left` | `can` | `None` | Baked mesh |
| 7 | **Chomp** | `throw-paper` | No | 7061 | 7061 | 7061 | 4600 | `cringe` | `jump` | `teeth` | `None` | Baked mesh |
| 8 | **CigarSmoke** | `cigar-smoke` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 9 | **ClipOnTie** | `throw-paper` | No | 7508 | 7508 | 6818 | 3310 | `conked` | `sidestep-left` | `clip-on-tie` | `SA_powertie_throw` | Baked mesh, parabolic arc |
| 10 | **Crunch** | `throw-object` | No | 7232 | 7232 | 7232 | 4710 | `cringe` | `sidestep-left` | `None` | `None` | Particle omitted |
| 11 | **Demotion** | `magic1` | No | 7503 | 7503 | 7503 | 2300 | `cringe` | `duck` | `None` | `SA_demotion` | Particle omitted |
| 12 | **DoubleTalk** | `speak` | No | 7042 | 7042 | 7042 | 4300 | `duck` | `duck` | `None` | `SA_filibuster` | Particle omitted |
| 13 | **Downsize** | `magic2` | No | 8070 | 8070 | 8070 | 2310 | `juggle` | `sidestep-left` | `None` | `None` | Particle omitted |
| 14 | **EvictionNotice** | `throw-paper` | No | 7608 | 7608 | 7608 | 3410 | `conked` | `jump` | `shredder-paper` | `None` | Baked mesh, parabolic arc |
| 15 | **EvilEye** | `glower` | No | 5852 | 5852 | 5852 | 4140 | `duck` | `duck` | `None` | `SA_evil_eye` | Standard |
| 16 | **Filibuster** | `speak` | No | 7042 | 7042 | 7042 | 2460 | `cringe` | `sidestep-left` | `None` | `SA_filibuster` | Particle omitted |
| 17 | **FillWithLead** | `pencil-sharpener` | No | 7600 | 9066 | 9066 | 4500 | `conked` | `sidestep-left` | `None` | `None` | Particle omitted |
| 18 | **FingerWag** | `finger-wag` | No | 5492 | 5492 | 5492 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 19 | **Fired** | `magic2` | No | 5500 | 5500 | 5500 | 1510 | `cringe` | `sidestep-left` | `None` | `SA_hot_air` | Particle omitted |
| 20 | **FiveOClockShadow** | `glower` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 21 | **FloodTheMarket** | `glower` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 22 | **FountainPen** | `pen-squirt` | No | 6018 | 6018 | 6018 | 1820 | `conked` | `duck` | `None` | `SA_fountain_pen` | Particle omitted |
| 23 | **FreezeAssets** | `glower` | No | 6470 | 6470 | 6470 | 3510 | `cringe` | `sidestep-left` | `stormcloud` | `None` | Baked mesh |
| 24 | **Gavel** | `gavel` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 25 | **GlowerPower** | `glower` | No | 4052 | 4052 | 4052 | 1610 | `slip-backward` | `sidestep-left` | `dagger` | `SA_glower_power` | Baked mesh, parabolic arc |
| 26 | **GuiltTrip** | `magic1` | Yes | 3792 | 3792 | 3792 | 1510 | `slip-forward` | `jump` | `None` | `SA_guilt_trip` | Particle omitted |
| 27 | **HalfWindsor** | `throw-paper` | No | 5412 | 5412 | 5412 | 3410 | `conked` | `sidestep-left` | `half-windsor` | `None` | Baked mesh, parabolic arc |
| 28 | **HangUp** | `phone` | No | 8292 | 8292 | 8292 | 5510 | `slip-backward` | `jump` | `None` | `SA_hangup` | Standard |
| 29 | **HeadShrink** | `magic1` | No | 8600 | 8600 | 8600 | 2110 | `cringe` | `sidestep-left` | `None` | `None` | Particle omitted |
| 30 | **HotAir** | `speak` | No | 7500 | 7500 | 7500 | 3610 | `cringe` | `sidestep-left` | `None` | `SA_hot_air` | Particle omitted |
| 31 | **Jargon** | `speak` | No | 9418 | 9418 | 9418 | 2900 | `conked` | `duck` | `None` | `SA_jargon` | Particle omitted |
| 32 | **Legalese** | `speak` | No | 7042 | 7042 | 7042 | 1910 | `cringe` | `sidestep-left` | `None` | `None` | Particle omitted |
| 33 | **Liquidate** | `magic1` | No | 8500 | 8500 | 8500 | 3510 | `melt` | `sidestep-left` | `stormcloud` | `SA_liquidate` | Baked mesh |
| 34 | **MarketCrash** | `throw-paper` | No | 6757 | 6757 | 6757 | 4200 | `cringe` | `sidestep-left` | `newspaper` | `None` | Baked mesh |
| 35 | **MumboJumbo** | `speak` | No | 7042 | 7042 | 7042 | 3210 | `cringe` | `sidestep-left` | `None` | `SA_mumbo_jumbo` | Particle omitted |
| 36 | **ParadigmShift** | `magic2` | Yes | 6703 | 6703 | 6703 | 4650 | `think` | `jump` | `None` | `SA_paradigm_shift` | Particle omitted |
| 37 | **PeckingOrder** | `throw-object` | No | 6432 | 6432 | 6432 | 4210 | `cringe` | `sidestep-left` | `None` | `None` | Standard |
| 38 | **PickPocket** | `pickpocket` | No | 4167 | 4167 | 4167 | 610 | `cringe` | `sidestep-left` | `None` | `SA_pick_pocket` | Standard |
| 39 | **PinkSlip** | `throw-paper` | No | 5822 | 5822 | 5822 | 3710 | `jump` | `jump` | `pink-slip` | `SA_pink_slip` | Baked mesh |
| 40 | **PlayHardball** | `throw-paper` | No | 5362 | 6062 | 5362 | 3900 | `conked` | `sidestep-left` | `baseball` | `SA_hardball_impact_only` | Baked mesh |
| 41 | **PoundKey** | `phone` | No | 5800 | 5875 | 5875 | 2710 | `cringe` | `sidestep-left` | `None` | `SA_hangup` | Particle omitted |
| 42 | **PowerTie** | `throw-paper` | No | 7508 | 7508 | 6818 | 3310 | `conked` | `sidestep-left` | `power-tie` | `SA_powertie_throw` | Baked mesh, parabolic arc |
| 43 | **PowerTrip** | `magic1` | Yes | 4092 | 4092 | 4092 | 1810 | `slip-forward` | `jump` | `None` | `None` | Particle omitted |
| 44 | **Quake** | `quick-jump` | Yes | 6394 | 6394 | 6394 | 2900 | `slip-forward` | `jump` | `None` | `None` | Standard |
| 45 | **RazzleDazzle** | `smile` | No | 4892 | 4892 | 4892 | 2610 | `cringe` | `sidestep-left` | `None` | `SA_razzle_dazzle` | Particle omitted |
| 46 | **RedTape** | `throw-object` | No | 7442 | 7442 | 7442 | 3410 | `struggle` | `jump` | `redtape` | `SA_red_tape` | Baked mesh, parabolic arc |
| 47 | **ReOrg** | `magic3` | No | 6500 | 6500 | 6500 | 3800 | `neutral` | `duck` | `None` | `None` | Particle omitted |
| 48 | **RestrainingOrder** | `throw-paper` | No | 7462 | 7462 | 7462 | 3410 | `conked` | `sidestep-left` | `shredder-paper` | `None` | Baked mesh, parabolic arc |
| 49 | **Rolodex** | `roll-o-dex` | No | 8008 | 8208 | 7708 | 3810 | `conked` | `sidestep-left` | `None` | `SA_rolodex` | Particle omitted |
| 50 | **RubberStamp** | `rubber-stamp` | No | 7608 | 7608 | 7608 | 3410 | `conked` | `sidestep-left` | `CancelledTop` | `SA_rubber_stamp` | Baked mesh, parabolic arc |
| 51 | **RubOut** | `hold-eraser` | No | 6408 | 6408 | 6408 | 2210 | `conked` | `jump` | `None` | `SA_rubout` | Particle omitted |
| 52 | **Sacked** | `throw-paper` | No | 6942 | 6942 | 6942 | 5680 | `struggle` | `sidestep-left` | `sandbag` | `None` | Baked mesh |
| 53 | **SandTrap** | `golf-club-swing` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 54 | **Schmooze** | `speak` | No | 7878 | 8578 | 8778 | 2900 | `conked` | `duck` | `None` | `None` | Particle omitted |
| 55 | **Shake** | `stomp` | Yes | 5694 | 5694 | 5694 | 2200 | `slip-forward` | `jump` | `None` | `None` | Standard |
| 56 | **Shred** | `shredder` | No | 5400 | 5400 | 8650 | -90 | `conked` | `sidestep-left` | `None` | `SA_shred` | Particle omitted |
| 57 | **SongAndDance** | `song-and-dance` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 58 | **Spin** | `magic3` | No | 6600 | 6600 | 6600 | 3800 | `duck` | `sidestep-left` | `None` | `None` | Particle omitted |
| 59 | **Synergy** | `magic3` | Yes | 3992 | 3992 | 3992 | 1710 | `slip-forward` | `jump` | `None` | `None` | Particle omitted |
| 60 | **Tabulate** | `phone` | No | 7408 | 7408 | 7408 | 3210 | `conked` | `sidestep-left` | `None` | `None` | Particle omitted |
| 61 | **TeeOff** | `golf-club-swing` | No | 8916 | 6700 | 6700 | 4718 | `conked` | `duck` | `golf-ball` | `SA_tee_off` | Baked mesh, parabolic arc |
| 62 | **ThrowBook** | `throw-object` | No | 5492 | 7608 | 5875 | 2710 | `slip-backward` | `sidestep-left` | `None` | `SA_finger_wag` | Particle omitted |
| 63 | **Tremor** | `stomp` | Yes | 5694 | 5694 | 5694 | 2200 | `slip-forward` | `jump` | `None` | `SA_tremor` | Standard |
| 64 | **Watercooler** | `watercooler` | No | 5285 | 5285 | 5625 | -490 | `cringe` | `sidestep-left` | `None` | `None` | Standard |
| 65 | **Withdrawal** | `magic1` | No | 4500 | 4500 | 4500 | 1210 | `cringe` | `duck` | `None` | `SA_withdrawl` | Particle omitted |
| 66 | **WriteOff** | `hold-pencil` | No | 5692 | 5692 | 5692 | 3410 | `slip-forward` | `sidestep-left` | `checkmark` | `None` | Baked mesh, parabolic arc |

## 4. Per-Cog Attack Availability

Below is the mapping from each Cog species code to its attacks in `SuitAttributes`:

| Code | Dept | Species | Attack 0 | Attack 1 | Attack 2 | Attack 3 | Attack 4 | Attack 5 |
|---|---|---|---|---|---|---|---|---|
| `f` | - | - | `PoundKey` | `Shred` | `ClipOnTie` | - | - | - |
| `p` | - | - | `FountainPen` | `RubOut` | `FingerWag` | `WriteOff` | `FillWithLead` | - |
| `ym` | - | - | `RubberStamp` | `RazzleDazzle` | `Synergy` | `TeeOff` | - | - |
| `mm` | - | - | `Demotion` | `FingerWag` | `FountainPen` | `BrainStorm` | `BuzzWord` | - |
| `ds` | - | - | `Canned` | `Downsize` | `PinkSlip` | `Sacked` | - | - |
| `hh` | - | - | `FountainPen` | `GlowerPower` | `HalfWindsor` | `HeadShrink` | `Rolodex` | - |
| `cr` | - | - | `Canned` | `EvilEye` | `PlayHardball` | `PowerTie` | - | - |
| `tbc` | - | - | `CigarSmoke` | `FloodTheMarket` | `SongAndDance` | `TeeOff` | - | - |
| `bf` | - | - | `RubberStamp` | `Shred` | `Watercooler` | `PickPocket` | - | - |
| `b` | - | - | `EvictionNotice` | `RedTape` | `Withdrawal` | `Liquidate` | - | - |
| `dt` | - | - | `RubberStamp` | `BounceCheck` | `BuzzWord` | `DoubleTalk` | `Jargon` | `MumboJumbo` |
| `ac` | - | - | `Shake` | `RedTape` | `Rolodex` | `HangUp` | - | - |
| `bs` | - | - | `GuiltTrip` | `RestrainingOrder` | `FingerWag` | - | - | - |
| `sd` | - | - | `ParadigmShift` | `Quake` | `Spin` | `WriteOff` | - | - |
| `le` | - | - | `EvilEye` | `Jargon` | `Legalese` | `PeckingOrder` | - | - |
| `bw` | - | - | `PowerTrip` | `ThrowBook` | - | - | - | - |
| `sc` | - | - | `Watercooler` | `BounceCheck` | `ClipOnTie` | `PickPocket` | - | - |
| `pp` | - | - | `BounceCheck` | `FreezeAssets` | `FingerWag` | - | - | - |
| `tw` | - | - | `Fired` | `GlowerPower` | `FingerWag` | `FreezeAssets` | `BounceCheck` | - |
| `bc` | - | - | `Audit` | `Calculate` | `Tabulate` | `WriteOff` | - | - |
| `nc` | - | - | `Audit` | `Calculate` | `Crunch` | `Tabulate` | - | - |
| `mb` | - | - | `Liquidate` | `MarketCrash` | `PowerTie` | - | - | - |
| `ls` | - | - | `Bite` | `Chomp` | `PlayHardball` | `WriteOff` | - | - |
| `rb` | - | - | `PowerTrip` | `TeeOff` | - | - | - | - |
| `cc` | - | - | `FreezeAssets` | `PoundKey` | `DoubleTalk` | `HotAir` | - | - |
| `tm` | - | - | `ClipOnTie` | `PickPocket` | `Rolodex` | `DoubleTalk` | - | - |
| `nd` | - | - | `RazzleDazzle` | `Rolodex` | `Synergy` | `PickPocket` | - | - |
| `gh` | - | - | `RubberStamp` | `FountainPen` | `Filibuster` | `Schmooze` | - | - |
| `ms` | - | - | `BrainStorm` | `HalfWindsor` | `Quake` | `Shake` | `Tremor` | - |
| `tf` | - | - | `EvilEye` | `HangUp` | `RazzleDazzle` | `RedTape` | - | - |
| `m` | - | - | `BuzzWord` | `ParadigmShift` | `PowerTrip` | `Schmooze` | `TeeOff` | - |
| `mh` | - | - | `PowerTrip` | `RazzleDazzle` | - | - | - | - |
