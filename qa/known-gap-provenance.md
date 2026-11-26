# KNOWN GAP floors for ttc_near / dd_near: provenance

Written 2026-11-23 by the claude-3ds-gates lane for the QA ruling of 2026-11-23 (task 2). Branch
feature/3ds-visual-fixes. Short answer: the floors were set by a lane task prompt, not by a logged QA
ruling, and the "source-limited" label is not backed by the evidence on file. The evidence on file
points at the emulator draw path (Azahar renders the same baked bytes about 2x softer than Panda), and
that cause was never isolated, so our runtime is not ruled out.

## 1. Which commit set the floors

- Commit b34bbe2b12819841994e1be049e3c1d0504e1fe5 (2026-11-22 03:37 EST, "near detail: opt 2100 into
  c150 sharpen; ttc/dd near are a KNOWN GAP with floors (QA-84 ruling a)"). It added `KNOWN_GAP` to
  `tools/fidelity/compare_views.py` (ttc_near a 442, b 0.62, d 1.49; dd_near a 258, b 0.66, d 1.11),
  compared at printed precision, plus `docs/3ds-near-detail.md` and the tests.
  `git log -S KNOWN_GAP -- tools/fidelity/compare_views.py` returns only this commit before today's
  399eced6d3d4c1842e46df17026b6dd19cc834c8.
- The numbers come word for word from the qa19 lane's task prompt,
  `/workspace/tt-agents/claude-3ds-qa19/PROMPT-r4.md` item 2: "Record them in tools/fidelity as
  KNOWN GAP with floors from the sweep off-rows: ttc (a) >= 442, (b) >= 0.62x, (d) <= 1.49x; dd (a)
  >= 258, (b) >= 0.66x, (d) <= 1.11x". That prompt calls itself "QA ruled on QA-20261119-84 (a):
  option (1) plus a ratchet".

## 2. What the QA log says

- `/workspace/qa/log.jsonl` has one entry for QA-20261119-84 (line 488, 2026-11-21 09:30, verdict FIX
  on ec6fbb6). Its note is the ringing finding and "Ruling: masked r11 accepted; ringing guard added
  to the bar". It sets no floor, no KNOWN GAP and no ratchet.
- No later Toontown 3DS entry exists in the log (the last one is QA-84). A grep of the whole log for
  "known gap", "ratchet", "floor", "source-limited", "442" and "0.62x" finds nothing about this gate.
- The "options" in the prompt are the qa19 lane's own list at the end of
  `claude-3ds-qa19/qa-r3/sharpen-sweep.md` ("Options for QA: (1) accept c150 or c250 for 2100 only
  ..."). The evidence VERDICT for ec6fbb6 (`/workspace/qa/evidence/tt-3ds-qa19-ec6fbb6/VERDICT.md`)
  does not mention floors either.
- So no logged QA ruling set these floors. They are a prompt instruction whose ruling is not on file.

## 3. Where the numbers come from

They are the "off" rows of the qa19 sharpen sweep, rounded to the nearest step:

| row | (a) masked detail | (b) off-edge | (d) overshoot |
|---|---|---|---|
| ttc_near off | 442 (stock 408) | 70.7 vs 114.5 = 0.62x | 0.090 vs 0.060 = 1.49x |
| dd_near off | 258 (stock 231) | 34.5 vs 52.7 = 0.66x | 0.044 vs 0.040 = 1.11x |

- Sweep table: `claude-3ds-qa19/qa-r3/sharpen-sweep.md` (HEAD 04560e4); the off rows are frames from
  the r59 gate4 run (`/workspace/shots/3ds-visual/r59-qa/gate4/`), scored in
  `claude-3ds-qa19/qa-r3/3ds-visual/gate4.txt` lines 5-12 and 48-55.
- Stock run: `claude-3ds-qa19/qa-r3/3ds-visual/stock_meta.json`: stock_detail.py, fovy 55, 400x240,
  TT_RESOURCES d00df01af876cc9c5d1a22329f394a9a50245e9c, render_sha256
  ab9c41e3223032c2a9d87b238b835981b0d60c9ebfca1d068f02ffefcfe96be2, PNG sha256 ttc_near
  8e36898c2721c2bd165acedb25e88f30ccd49e978b8bdb0797a5a19026eef9ba, dd_near
  0f8388874adb414c8a7d20bd6ba2de3c74a8c5c04473fb426d47cb4a4747b14c (equal to `STOCK_PIN`; this box
  rendered the same bytes again on 2026-11-23, lane STATUS round 1).
- Bake: the off rows use the unsharpened zones, checked byte-identical to the installed set in
  `claude-3ds-qa19/qa-r3/3ds-visual/identity.log`:
  2000.tz 3dec91182254bd6ac7b883c605fee03f22e1d41f5c56a73ac6a888210c431b7e,
  1000.tz 9cbbae17938d029431fe66666834e42809739297990926de72a53c009c4603df.
  The box gates of 2026-11-23 ran on the same 2000 / 1000 bytes.
- Box gates (source 53b780561562657a7f4f0efe05f6f3425fa59906, three runs, frames byte-identical):
  raw ttc_near a 442.499893, b 0.617313, d 1.493757; dd_near a 257.717437, b 0.655692, d 1.110967. Rounded to the
  nearest step these are the floors exactly, so the floors were set at zero margin and passed only
  because the gate rounded the measurement too (fixed in 399eced).
- Crops (stock / Old / New): `claude-3ds-qa19/qa-r4/crops/{ttc,dd}_near_stock_old_new_4x.png`,
  `claude-3ds-qa19/qa-r3/3ds-visual/head_stock_old_new_4x.png`, sweep crops
  `claude-3ds-qa19/qa-r3/3ds-visual/sweep_4x.png`, and this lane's
  `/workspace/shots/claude-3ds-gates/{ttc,dd}_near_crop2x.png`.

## 4. Is the gap source-limited? What the evidence shows

What is on file:
- The sweep shows that no unsharp setting passes all four columns on ttc_near or dd_near
  (ttc (d) passes 1.5x before (a) / (b) clear). That proves sharpening cannot buy the gap. It does not
  prove the source texels are the limit.
- `claude-3ds-qa19/qa-r2/3ds-visual/gates.md` lines 42-45: "Panda renders of the same baked .tz bytes
  at the same cameras (offline proxy) scored about 2x the Azahar frames (880/408/433 masked); the door,
  wall and awning tiles came out softer in Azahar while crates matched. Mip bias and the baked mip
  chain did not explain it." The same claim is in commit ec6fbb6 and in AGENTS.md section 4.
  If that holds, the baked data carries far more than stock's 408 / 231 of detail, so the gap is not in
  the source assets or the bake: it is in how the frame is drawn from those bytes.
- The density test cited in the sweep ("Doubling TT_ATLAS_DENSITY was tried in round 1, no visible gain
  on the door") exists only as a note (a lane memory line: "TT_ATLAS_DENSITY=1.0 changed TTC by 4%,
  detail unchanged"). There is no run log, frame or bake hash for it, and it was scored against the
  old unmasked bar.

What is missing:
- The Panda proxy that gave 880 / 408 / 433 is not in the repo or any QA pack (no script, no frames,
  no log). Its numbers cannot be reproduced from what is on file.
- Nobody isolated why Azahar is softer: Azahar's texture filtering, our runtime's sampler / LOD state,
  or the ETC1 decode path. So "renderer, not our code" is not shown either; the runtime is one of the
  candidates.
- No hardware frame exists. Azahar's sampling is not the 3DS GPU, so the gap may differ on hardware.

Conclusion: the label "source-limited" in the gate output is not supported. The best supported
reading is "draw-path limited (Azahar renders the baked tiles softer than Panda), cause not isolated".
The floors are a zero-margin ratchet from a prompt, not a QA ruling. This lane does not change the
label text the qa19 prompt fixed; QA should rule on the wording and on whether the floors stand.
