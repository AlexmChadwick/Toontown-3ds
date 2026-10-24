# Cog boss runtime

This document tracks the native boss implementation and its upstream contract. The
reference tree is read-only; all citations are repository paths under
`ref/ref/open-toontown`.

## Entry and ownership

`toontown/building/DistributedBossElevatorAI.py::checkBoard` rejects a toon below
`minLaff` or without `readyForPromotion(dept)`. Elevator closure creates the boss
office and sends `setBossOfficeZone`; the forced path uses
`setBossOfficeZoneForce`. A production client must retain those entry checks.
`AI_DEV=1 tools/server/start.sh` already patches the boss elevator check in memory,
restores laff and stocks the gag pouch (`tools/server/ai_dev.py`).

The AI owns boss phases, Cog rounds, attacks and damage. The client acknowledges
movie barriers, renders the phase, submits permitted player actions and follows
the server's result. Incoming phase strings must never be used as outgoing fields.
The common wire contract is `etc/toon.dc::DistributedBossCog`; VP-specific fields
are in `DistributedSellbotBoss`. The DC hash remains 547155168.

## VP implementation checklist

The required path is lobby elevator, intro, two Cog rounds through the existing
Battle API, pie combat, victory/reward and return. Body parts attach legs -> torso
at `joint_pelvis`, then torso -> head at `joint34`; treads attach to `joint_axle`
(`toontown/suit/BossCog.py::generateBoss`). Animations are baked offline as TTA1.
The arena is `phase_9/models/cogHQ/BossRoomHQ`
(`toontown/suit/DistributedSellbotBoss.py::loadEnvironment`).

Implementation and verification results will be recorded here as work completes.
