"""Opt-in AI import-hook contract; no live server or ref writes. Python -I -B."""

import os
from pathlib import Path
import runpy
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
patch = runpy.run_path(str(ROOT / "tools/server/ai_dev.py"))["_patch"]


class Tasks:
    def __init__(self):
        self.pending = []

    def doMethodLater(self, delay, callback, name):
        self.pending.append((delay, callback, name))


class Building:
    def __init__(self, air, zone, number):
        self.air = air
        self.canonicalZoneId = zone
        self.zoneId = zone
        self.block = number
        self.doId = number + 100
        self.entered = 0
        self.calls = []

    def enterToon(self):
        self.entered += 1

    def uniqueName(self, name):
        return f"{name}-{self.doId}"

    def isDeleted(self):
        return False

    def isToonBlock(self):
        return True

    def cogdoTakeOver(self, *args):
        self.calls.append(args)


tasks = Tasks()
module = SimpleNamespace(DistributedBuildingAI=Building, taskMgr=tasks)
os.environ.pop("AI_DEV_COGDO", None)
original = Building.enterToon
patch("toontown.building.DistributedBuildingAI", module)
assert Building.enterToon is original
os.environ["AI_DEV_COGDO"] = "1"
patch("toontown.building.DistributedBuildingAI", module)
air = SimpleNamespace()
other = Building(air, 3100, 0)
other.enterToon()
assert not tasks.pending
first = Building(air, 2100, 1)
second = Building(air, 2132, 2)
first.enterToon()
second.enterToon()
assert first.entered == second.entered == 1
assert len(tasks.pending) == 1 and tasks.pending[0][0] == 5
assert tasks.pending[0][1](SimpleNamespace(done="done")) == "done"
assert first.calls == [("s", 0, 1)] and not second.calls
repo = SimpleNamespace(forcedCogdoGame="")
module = SimpleNamespace(simbase=repo)
for game in ("", "unknown", "flying"):
    os.environ["AI_DEV_COGDO_GAME"] = game
    patch("toontown.cogdominium.DistributedCogdoInteriorAI", module)
    assert repo.forcedCogdoGame == ""
for game in ("maze", "crane", "boardroom"):
    os.environ["AI_DEV_COGDO_GAME"] = game
    patch("toontown.cogdominium.DistributedCogdoInteriorAI", module)
    assert repo.forcedCogdoGame == game
print("Cogdo AI opt-out, one canonical TTC takeover, prototype whitelist: PASS")
