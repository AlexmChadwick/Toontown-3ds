198a68e2bf971e246e84762876558133fbbb90b8

# Round 7 evidence pack: QA-20261119-168

Tested source and pre-pack remote tip: `198a68e2bf971e246e84762876558133fbbb90b8`, branch `feature/3ds-visual-fixes`.
App SHA-256: `12fad941186dc1f7c606195db4278d74b4b0159983d30aaf464332611abae44d`.
Stock assets: `d00df01af876cc9c5d1a22329f394a9a50245e9c`. All three gates accepted STOCK_PIN and the same lens.
This wrap extracts existing files only. No build, gate, sweep, fidelity change or threshold change was performed.

## Local gate result: GREEN, three complete runs

| Run | compare_views.py exit | run_gate.sh exit | Views |
| --- | --- | --- | --- |
| 1 | 0 | 0 | 8/8 PASS |
| 2 | 0 | 0 | 8/8 PASS |
| 3 | 0 | 0 | 8/8 PASS |

The eight views are ttc_near, ttc_mid, ttc_far, ttc_tree, street_near, street_far, dd_near and dd_far, each captured in Old and New profiles.
Interrupted captures are excluded. Gate 3 finished at 04:12:39 EST on 2026-11-26; the sweep queue finished at 04:19:51 EST.

Evidence: [gate 1](/workspace/tt-agents/claude-3ds-gates/qa/r7/gate1/gate.txt), [gate 2](/workspace/tt-agents/claude-3ds-gates/qa/r7/gate2/gate.txt), [gate 3](/workspace/tt-agents/claude-3ds-gates/qa/r7/gate3/gate.txt).
Each directory also contains run.log, stock.txt, plan.txt, stock PNGs, Old/New views and client/emulator logs. Their first lines contain the full tested SHA.
Queue evidence: [gate queue](/workspace/tt-agents/claude-3ds-gates/qa/r7/gate-queue.log), [resumed queue](/workspace/tt-agents/claude-3ds-gates/qa/r7/resumed-queue.log).
[File validation](/workspace/tt-agents/claude-3ds-gates/qa/r7/pack-validation.log) checked the saved exits, eight PASS records per run, complete autotests, profile selection and SHA headers. Top and bottom PNGs have one unique hash per profile/view across all three runs.

## Masked and unmasked detail

All metrics use the gate's shared toon/nametag exclusion. Here, unmasked means the awning cell remains included.
Only ttc_near and dd_near exclude x 280-320, y 0-40 for the r11 comparison and (b), per QA-168.
The 1.3x-stock (a) requirement remains unmasked. The awning anti-strobe mip remains in place.
The following values apply to all three runs; detail is the smaller of Old/New and ringing is the larger.

| View | Stock (a), unmasked | Tip (a), unmasked | Tip/stock | 1.3x stock bar | Tip (a), awning-masked | r11 masked measured / gate floor |
| --- | --- | --- | --- | --- | --- | --- |
| ttc_near | 407.808092 | 544.638032 | 1.335525x | 530.150520 | 554.840422 | 375.502334 / 375 |
| dd_near | 230.795694 | 308.579908 | 1.337026x | 300.034402 | 314.353277 | 212.312703 / 212 |
| street_near | 195.043270 | 270.526130 | 1.387006x | 253.556251 | unchanged | 167.132895 / 167 |

| View | (b) unmasked ratio | (b) gate ratio | (b) minimum | (c) high-pass ratio | (c) maximum | (d) overshoot ratio | (d) maximum |
| --- | --- | --- | --- | --- | --- | --- | --- |
| ttc_near | 1.383049x | 1.683675x masked | 1.0x | 1.112455x | 1.2x | 1.484834x | 1.5x |
| dd_near | 0.835383x | 1.259791x masked | 1.0x | 1.128516x | 1.2x | 1.466600x | 1.5x |
| street_near | 1.159746x | 1.159746x unmasked | 1.0x | 1.112548x | 1.2x | 0.915739x | 1.5x |

DD's unmasked (b) remains below 1.0x; the authorized masked metric passes. TTC's overshoot has only 0.015166x margin.
The printed r11 bars are 375/212/167; measured r11 values above are retained explicitly. Unmasked r11: TTC 491.397334, DD 281.155139.
[All nine run/view rows with stock / Old / New values](/workspace/tt-agents/claude-3ds-gates/qa/r7/near-table.md), [r11 frame hashes and measurements](/workspace/tt-agents/claude-3ds-gates/qa/r7/r11-images.log).

## FPS and memory

All 48 shot-time samples are at least 29.9 fps (one is 30.0). Every row's memory matches the corresponding round 6 profile/view.
The table shows stable memory values across runs. Units are KiB; heap is used, linear and VRAM are free.

| View | Old/New minimum FPS | Old/New heap used | Old/New linear free | Old/New VRAM free |
| --- | --- | --- | --- | --- |
| ttc_near | 29.9 / 29.9 | 2425 / 2425 | 12540 / 12540 | 4194 / 4194 |
| ttc_mid | 29.9 / 29.9 | 2424 / 2424 | 12297 / 12294 | 4194 / 4194 |
| ttc_far | 29.9 / 29.9 | 2426 / 2427 | 11455 / 10896 | 4194 / 4194 |
| ttc_tree | 29.9 / 29.9 | 2425 / 2425 | 12314 / 12398 | 4194 / 4194 |
| street_near | 29.9 / 29.9 | 2605 / 2605 | 12470 / 12310 | 4194 / 4194 |
| street_far | 29.9 / 29.9 | 2605 / 2605 | 12379 / 12562 | 4194 / 4194 |
| dd_near | 29.9 / 29.9 | 2420 / 2420 | 13631 / 13631 | 4194 / 4194 |
| dd_far | 29.9 / 29.9 | 2420 / 2420 | 12857 / 12857 | 4194 / 4194 |

[Full 48-row table](/workspace/tt-agents/claude-3ds-gates/qa/r7/stats-table.md) includes CPU time, draws and triangles, using the last sample at or before each selected frame's logged time.
Old ttc_far still reports 15025 triangles, above the nominal 15k envelope; this pre-existing count is not resolved by this pack.
Street_near's 74/84 Old/New draw ratio is waived by the existing scorer because its frames are identical. See gate logs for the explicit waiver.

## Crops and visual observations

Columns in near comparisons are stock (55 degrees) / r11 New / tip Old / tip New.

- [TTC full comparison](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/ttc_near_full.png), [door edge at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/ttc_near_entrance_4x.png), [cobbles at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/ttc_near_cobbles_4x.png), [awning at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/ttc_near_awning_4x.png).
- [DD full comparison](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/dd_near_full.png), [woodwork at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/dd_near_woodwork_4x.png), [boardwalk at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/dd_near_boardwalk_4x.png), [awning at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/dd_near_awning_4x.png).
- [Street full comparison](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/street_near_full.png), [facade at 4x](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops/street_near_facade_4x.png).
- Remaining saved comparisons and bottom screens: [crop directory](/workspace/tt-agents/claude-3ds-gates/qa/r7/crops).

The inspected TTC and DD comparisons show clearer door outlines, woodwork and cobbles than r11, with closely matching Old/New output. The tip retains a smooth edge-on awning instead of the stock/r11 alias pattern. These fixed views do not establish motion stability or hardware performance.
[Bake audit](/workspace/tt-agents/claude-3ds-gates/qa/r7/bake-audit.log) records unchanged geometry, texture layouts, far textures, awning payloads and standalone terminal mean mips. [Exact gate data hashes](/workspace/tt-agents/claude-3ds-gates/qa/r7/data.log).

## Facility and boss sweeps

| Suite | Coverage | Runner exit | Saved-frame rescore exit | Recorded result |
| --- | --- | --- | --- | --- |
| Facility | 31 rooms, 4-12 frames each | 0 | 0 | 340 PASS rows |
| Boss | vp / cfo / cj / ceo, 16 frames each | 0 | 0 | 64 PASS rows |

[Facility logs and manifest](/workspace/tt-agents/claude-3ds-gates/qa/r7/sweeps/level), [boss logs and manifest](/workspace/tt-agents/claude-3ds-gates/qa/r7/sweeps/boss), [queue](/workspace/tt-agents/claude-3ds-gates/qa/r7/sweep-queue.log), [saved-manifest verification](/workspace/tt-agents/claude-3ds-gates/qa/r7/pack-sweeps.log).
Contact sheets: `/workspace/shots/claude-3ds-gates/r7/sweeps/{level,boss}/`. SHA-256 inventories are in each evidence directory as sheets.sha256.

The sweep scores do not establish visually clean output. Inspected examples visibly contain magenta exposed or missing-geometry regions: VP walls, CFO doorways, CJ's stock floor and Bossbot Presidents Room. In the CJ comparison, the baked side has a floor where the stock side is magenta. The source/rendering cause of these regions remains unresolved. The scorer still exits 0, so these examples must remain visible in the evidence and require follow-up visual assessment.
Representative saved images: [VP](/workspace/shots/claude-3ds-gates/r7/alex-progress/boss-vp.png), [CFO](/workspace/shots/claude-3ds-gates/r7/alex-progress/boss-cfo.png), [CJ](/workspace/shots/claude-3ds-gates/r7/alex-progress/boss-cj.png), [Presidents Room](/workspace/shots/claude-3ds-gates/r7/alex-progress/level-BossbotPresidentsRm.png).

## groups.dc

[Final inventory](/workspace/tt-agents/claude-3ds-gates/qa/r7/groups-dc-final.log) searches source, RomFS, build outputs, linked references and the exact gate SD overlay.
No groups.dc is included in these build inputs. Generated DC hash remains 547155168 (otp.dc + toon.dc).
Canonical reference `/workspace/toontown/dc-ref/groups.dc` matches SHA-256 `7430afc3ffd9cd9beb9ef1b8e7e135fccf16b4a283200ca19601551eabbd5039`.

## Remote proof and remaining work

Before this documentation commit, HEAD and `git ls-remote origin refs/heads/feature/3ds-visual-fixes` both returned `198a68e2bf971e246e84762876558133fbbb90b8`.
The final fast-forward push and matching HEAD/ls-remote are recorded in [pack push proof](/workspace/tt-agents/claude-3ds-gates/qa/r7/pack-push.log); that log's first line is the final pack commit SHA. The pack commit changes documentation only; tested source remains the SHA above.

Remaining validation: independent QA disposition, real Old/New 3DS runs, motion/stereo/suspend checks, and assessment of sweep magenta regions and the pre-existing triangle-envelope excess. Local gates are green. This pack makes no independent QA acceptance claim.
