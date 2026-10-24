#!/usr/bin/env python3
"""Generate the measured Toon animation handoff from audited outputs and upstream emote IDs."""
import ast
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True
sys.path.insert(0,str(Path(__file__).resolve().parent))
from toon_anim_audit import tables
ROOT = Path(__file__).resolve().parents[2]
REPORTS = ROOT/'sdmc-out'
UPSTREAM = ROOT/'ref/ref/open-toontown'
FEATURES = {
    'neutral':'standing; Sleepy/Laugh reuse', 'run':'locomotion; cannon ground impact',
    'walk':'locomotion', 'jump-idle':'airborne loop', 'wave':'emote 0',
    'throw':'battle', 'pushbutton':'battle', 'cringe':'battle; emote 10',
    'conked':'battle; emote 20 subranges', 'sidestep-left':'battle dodge',
    'sidestep-right':'battle dodge', 'duck':'battle dodge', 'victory':'battle; emotes 6/15',
    'lose':'battle death', 'sad-walk':'battle sad locomotion', 'teleport':'teleport OUT',
    'book':'sticker book', 'jump':'Happy; teleport IN', 'jump-squat':'standing jump start',
    'jump-land':'standing jump landing', 'shrug':'emote 5', 'angry':'emotes 3/23',
    'sit-start':'trolley intoSit / SitStart', 'sit':'trolley seated loop',
    'swim':'ring swimming; cannon flight', 'tug-o-war':'tug pull / hold',
    'sad-neutral':'battle sad standing (not emote 2)', 'left':'emote 22 Delighted',
    'applause':'emote 9', 'confused':'emote 11', 'bow':'emote 13 shorts torso',
    'curtsy':'emote 13 skirt torso', 'bored':'emote 8 / Yawn', 'think':'emote 7 subrange',
    'castlong':'fishing cast windup', 'cast':'fishing release', 'fish-neutral':'fishing wait loop',
    'fish-again':'fishing recovery', 'fish-end':'fishing exit', 'reel':'fishing reel',
    'reel-H':'additional upstream reel variant', 'reel-neutral':'fishing reel recovery',
    'pole':'fishing equip', 'pole-neutral':'fishing ready loop',
    'slip-forward':'Belly Flop; Catch fallForward; cannon tower hit',
    'slip-backward':'Banana Peel; Catch fallBack', 'catch-neutral':'Catch idle',
    'catch-run':'Catch locomotion', 'catch-eatneutral':'Catch eat while still',
    'catch-eatnrun':'Catch eat while moving', 'bad-putt':'emote 21 Cry subrange',
}
# The function list is authoritative: localization has a duplicated Laugh dictionary key.
EMOTES = [
    ('Wave','doWave','wave','one-shot; torso 78/24 = 3.250 s'),
    ('Happy','doHappy','jump','one-shot; legs 40/24 = 1.667 s'),
    ('Sad','doSad','current body; sad eyes','3.000 s; facial-only; restore eyes'),
    ('Angry','doAnnoyed','angry','one-shot; torso 50/24 = 2.083 s; angry eyes'),
    ('Sleepy','doSleep','neutral at 0.4x','loop for 4.000 s; close eyes, stop blink/look, sleep thought; restore'),
    ('Shrug','doShrug','shrug','one-shot; torso 50/24 = 2.083 s'),
    ('Dance','doVictory','victory','one-shot; legs 123/24 = 5.125 s; not an endless dance loop'),
    ('Think','doThink','think frames 0..46 then 46..0','inclusive 47/24 each direction; 3.916667 s total'),
    ('Bored / Yawn','doBored','bored','one-shot 160/24 = 6.666667 s; sound at 2.2 s, then wait 4.466667 s'),
    ('Applause','doApplause','applause','one-shot; torso 51/24 = 2.125 s'),
    ('Cringe','doCringe','cringe','one-shot; torso 55/24 = 2.291667 s'),
    ('Confused','doConfused','confused','one-shot; torso 71/24 = 2.958333 s'),
    ('Belly Flop','doSlipForward','slip-forward','one-shot 55/24 = 2.291667 s; sound at 0.7 s'),
    ('Bow','doBow','bow or curtsy','shorts: 81/24 = 3.375 s; skirt: torso 73/24 or 81/24 = 3.041667 or 3.375 s'),
    ('Banana Peel','doSlipBackward','slip-backward','one-shot 67/24 = 2.791667 s; sound at 0.7 s'),
    ('Resistance Salute','doResistanceSalute','victory frames 0..9 then 9..0 at 0.75x','inclusive 10/24/0.75 each way; 1.111111 s; speech CustomSCStrings[4020]'),
    ('Laugh (legacy no-op)','doNothing','none','0 s; no track; preserve current animation'),
    ('Yes','doYes','procedural head pitch','0.950 s visual track; returned blocking duration is 0'),
    ('No','doNo','procedural head heading','0.950 s visual track; returned blocking duration is 0'),
    ('OK','doOk','none','0 s; no track'),
    ('Surprise','doSurprise','conked frames 9..50 then 70..101','one-shot gate 3.000 s; clamp source frame 101 to last valid 100; surprise eyes/muzzle'),
    ('Cry / Upset','doUpset','bad-putt frames 29..59 reverse then forward at 0.75x','31/24/0.75 each direction, then hold; gate 4.000 s; sad eyes/muzzle'),
    ('Delighted','doDelighted','left forward; wait 1 s; left reverse','gate 2.500 s; full inner sequence 16/24 + 1 + 16/24 = 2.333333 s; smile muzzle'),
    ('Furious','doFurious','angry','one-shot; torso 50/24 = 2.083 s; angry eyes/muzzle'),
    ('Laugh','doLaugh','neutral at 10x','loop for 2.000 s; laugh muzzle; restore rate 1x'),
]


def enum_name(name):
    return 'kAnim'+('PushButton' if name=='pushbutton' else ''.join(p[0].upper()+p[1:] for p in name.split('-')))


def main():
    t = tables()
    audit = json.loads((REPORTS/'toon_anim_audit.json').read_text())
    deterministic = json.loads((REPORTS/'toon_determinism.json').read_text())
    source = json.loads((REPORTS/'toon_sources.json').read_text())
    emote_tree = ast.parse((UPSTREAM/'toontown/toon/TTEmote.py').read_text())
    functions = next(n.value for n in emote_tree.body if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='EmoteFunc' for x in n.targets))
    assert [item.elts[0].id for item in functions.elts]==[e[1] for e in EMOTES]
    localizer = ast.parse((UPSTREAM/'otp/otpbase/OTPLocalizerEnglish.py').read_text())
    mapping = next(n.value for n in localizer.body if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='EmoteFuncDict' for x in n.targets))
    laughs = [ast.literal_eval(v) for k,v in zip(mapping.keys,mapping.values) if isinstance(k,ast.Constant) and k.value=='Laugh']
    assert laughs==[16,24]
    names = ['neutral','walk','run','jump-idle','wave']+[a for a in t['ANIMS'] if a not in ('neutral','walk','run','jump-idle','wave')]
    assert len(names)==51
    (REPORTS/'toon_anim_runtime.inc').write_text('// Proposed shared runtime enum/name table; append after IDs 0..4.\n'+''.join(
        f'TT_TOON_CLIP({enum_name(n)}, "{n}") // {i}\n' for i,n in enumerate(names)))
    (REPORTS/'toon_anim_timing.inc').write_text('// Source timings: identical for both LODs; names resolve via ToonAnim table.\n'+''.join(
        f'TT_TOON_SOURCE("{r["kind"]}", "{r["code"]}", {enum_name(r["clip"])}, {r["frames"]}, {r["fps"]:.1f}f)\n'
        for r in source['sources']))
    (REPORTS/'toon_emotes.json').write_text(json.dumps([dict(id=i,label=e[0],function=e[1],clip=e[2],timing=e[3]) for i,e in enumerate(EMOTES)],indent=2)+'\n')
    doc = """# Toon animation bake and runtime handoff

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

"""
    doc += f"""## Results and preservation proof

- **{audit['unchanged_files']} original binaries / {audit['unchanged_bytes']:,} bytes** SHA-256
  identical before and after: every old TTM, TTA and clothes TTX; zero mismatches.
- **{audit['new_files']} new TTA files / {audit['extra_sd_bytes']:,} bytes** extra SD storage
  ({audit['extra_sd_bytes']/1e6:.6f} MB, {audit['extra_sd_bytes']/1048576:.6f} MiB).
- Full set: **1,306 TTA + 104 TTM + 1 TTX**, {audit['total_binary_bytes']:,} bytes
  ({audit['total_binary_bytes']/1048576:.6f} MiB), plus `toon_manifest.json`.
- An independent repeat of all **{deterministic['byte_identical_files']} binary files** was byte-identical.
- `toon_manifest.json` intentionally grows to list new clips; old part metadata,
  file bytes, file paths, clothing maps and old clip references are unchanged.
- **{len(source['sources'])} source BAM paths** verified by `ls`, loaded without cache, and
  checked against `Toon.py` phase/suffix mappings and source frame counts. Ten
  Bow/Curtsy combinations are intentionally absent (listed in `toon_sources.json`).
- Every emitted TTA was checked for magic/version, part vertex count, frame count,
  12 fps sampling, section bounds/alignment, finite quantization/attachment data,
  padded vertex lanes, and expected leg/torso attachment tracks.

SHA-256 of the original hash inventory:
`{hashlib.sha256((REPORTS/'toon_before_hashes.json').read_bytes()).hexdigest()}`.
SHA-256 of the complete after inventory:
`{hashlib.sha256((REPORTS/'toon_after_hashes.json').read_bytes()).hexdigest()}`.

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
"""
    def counts(values):
        return '/'.join(map(str,values))
    for i,name in enumerate(names):
        r = audit['clips'][name]
        doc += f"| {i} | `{name}`{' +' if r['new'] else ''} | `P{r['phase']:g}/<prefix>{r['source_suffix']}.bam` | {counts(r['source_frames'])} -> {counts(r['frames'])} | {r['sd_bytes']/1024:.2f} | {r['linear_max']/1024:.2f} | {FEATURES[name]} |\n"
    doc += """
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
"""
    for i,(label,func,clip,timing) in enumerate(EMOTES):
        doc += f'| {i} | {label} (`{func}`) | {clip} | {timing} |\n'
    doc += """
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
"""
    target = ROOT/'docs/design/toon_anims.md'
    target.write_text(doc)
    print(f'Wrote {target}; 51 clip rows and 25 verified emote mappings')


if __name__=='__main__':
    main()
