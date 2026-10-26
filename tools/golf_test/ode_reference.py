#!/usr/bin/env python3
"""Compare the native GPH1 ball recorder with the stock ODE golf recorder.

This deliberately calls GolfHoleBase.trackRecordBodyFlight instead of duplicating
its QuickStep setup or recording rules.  It is an offline test tool; the 3DS
runtime never imports Panda, ODE, BAM, or any upstream Python.
"""
import argparse
import json
import subprocess
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / "ref" / "ref" / "open-toontown"
RESOURCES = ROOT / "ref" / "ref" / "resources"
if not RESOURCES.exists():
    RESOURCES = ROOT / "ref" / "resources"

sys.path.insert(0, str(UPSTREAM))

# The physics modules import ToontownGlobals but do not use it.  The pinned
# server tree expects Panda's optional otp extension, which stock panda3d does
# not ship, so provide only that unused import here.
sys.modules["toontown.toonbase.ToontownGlobals"] = types.ModuleType("ToontownGlobals")

from direct.showbase import ShowBaseGlobal
from direct.showbase.DirectObject import DirectObject
from direct.directnotify import DirectNotifyGlobal
from panda3d.core import loadPrcFileData

loadPrcFileData("golf-ode-reference", "window-type none\naudio-library-name null\n"
                "model-path %s\nmodel-cache-dir /private/tmp/golf-ode-cache" % RESOURCES)
base = ShowBaseGlobal.ShowBase()

from toontown.golf import GolfGlobals, GolfHoleBase, PhysicsWorldBase

# These modules normally receive the ShowBase globals through the game launcher.
for module in (GolfHoleBase, PhysicsWorldBase):
    module.loader = base.loader
    module.render = base.render
    module.globalClock = ShowBaseGlobal.globalClock
    module.eventMgr = base.eventMgr


class HoleHarness(DirectObject, PhysicsWorldBase.PhysicsWorldBase, GolfHoleBase.GolfHoleBase):
    notify = DirectNotifyGlobal.directNotify.newCategory("GolfOdeReference")

    def __init__(self, hole_id):
        DirectObject.__init__(self)
        PhysicsWorldBase.PhysicsWorldBase.__init__(self, 0)
        GolfHoleBase.GolfHoleBase.__init__(self, 0)
        self.holeInfo = GolfGlobals.HoleInfo[hole_id]
        self.ball = None
        self.rawRecording = []

    def curGolfBall(self):
        return self.ball

    def loadBlockers(self):
        # DistributedGolfHoleAI.loadBlockers with golf-all-blockers left at its
        # stock false default.  The unusual scalar in HoleInfo[9] is normalized
        # here only because the pinned source assumes Python 2's loose usage.
        locators = {}
        locator_num = 1
        locator = self.hardSurfaceNodePath.find("**/locator%d" % locator_num)
        while not locator.isEmpty():
            locators[locator_num] = locator
            locator_num += 1
            locator = self.hardSurfaceNodePath.find("**/locator%d" % locator_num)
        blockers = self.holeInfo.get("blockers", ())
        if isinstance(blockers, int):
            blockers = (blockers,)
        for locator_num, locator in locators.items():
            if locator_num in blockers:
                locator.getChildren().wrtReparentTo(locator.getParent())
            else:
                locator.removeNode()
        self.hardSurfaceNodePath.flattenStrong()

    def processRecording(self, error_mult=1.0):
        self.rawRecording = list(self.recording)
        return GolfHoleBase.GolfHoleBase.processRecording(self, error_mult)

    def prepare(self):
        self.loadLevel()
        self.setupSimulation()
        self.ball = self.createSphere(self.world, self.space,
                                      GolfGlobals.GOLF_BALL_DENSITY,
                                      GolfGlobals.GOLF_BALL_RADIUS, 1)[1]
        self.createRays()
        tee = self.terrainModel.find("**/tee0")
        if tee.isEmpty():
            raise RuntimeError("hole has no tee0")
        return tee.getPos(), self.holeBottomPos


def vec(v):
    return [float(v[0]), float(v[1]), float(v[2])]


def make_cases(hole_id, tee, cup):
    dx, dy = cup[0] - tee[0], cup[1] - tee[1]
    def aim(x, y):
        length = (x * x + y * y) ** 0.5
        return [x / length, y / length]
    # The direct shots cover weak/normal/full swings.  The angled full swing is
    # intentional: its hard-surface rebound is a regression case, while holes 6
    # and 12 include slick collision meshes in the stock set.
    return [
        {"name": "direct-soft", "power": 18.0, "dir": aim(dx, dy)},
        {"name": "direct-normal", "power": 45.0, "dir": aim(dx, dy)},
        {"name": "direct-full", "power": 82.0, "dir": aim(dx, dy)},
        {"name": "bank-angle", "power": 72.0, "dir": aim(dx - 0.35 * dy, dy + 0.35 * dx)},
        {"name": "cup-approach", "power": 1.0, "dir": [0.0, 1.0],
         "start": [cup[0], cup[1] - 0.4, cup[2] + GolfGlobals.GOLF_BALL_RADIUS]},
    ]


def ode_movie(hole_id, case):
    harness = HoleHarness(hole_id)
    tee, cup = harness.prepare()
    start = tee + (0, 0, GolfGlobals.GOLF_BALL_RADIUS)
    if "start" in case:
        start.set(*case["start"])
    end = harness.trackRecordBodyFlight(harness.ball, 0.0, case["power"], start,
                                        case["dir"][0], case["dir"][1])
    frames = [[int(frame), float(x), float(y), float(z)]
              for frame, x, y, z in harness.rawRecording]
    movie = [[int(frame), float(x), float(y), float(z)]
             for frame, x, y, z in harness.recording]
    result = {
        "start": vec(start), "cup": vec(cup), "frames": frames, "movie": movie,
        "inCup": bool(harness.ballInHoleFrame), "final": vec(end),
        "ballInFrame": int(harness.ballInHoleFrame),
    }
    harness.delete()
    return result


def native_movie(binary, hole_id, case, reference):
    gph = ROOT / "sdmc-out" / "3ds" / "toontown3ds" / "data" / "golf" / ("hole%d.gph" % hole_id)
    args = [str(binary), "--record", str(gph)]
    args += ["%.9g" % value for value in reference["start"]]
    args += ["%.9g" % case["power"], "%.9g" % case["dir"][0], "%.9g" % case["dir"][1]]
    args += ["%.9g" % value for value in reference["cup"]]
    output = subprocess.check_output(args, text=True)
    lines = output.splitlines()
    if not lines or not lines[0].startswith("inCup "):
        raise RuntimeError("bad native recorder output")
    frames = [[int(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])]
              for parts in (line.split() for line in lines[1:])]
    return {"frames": frames, "inCup": lines[0] == "inCup 1"}


def compare(reference, native):
    by_frame = {frame[0]: frame for frame in native["frames"]}
    pairs = [(frame, by_frame[frame[0]]) for frame in reference["frames"]
             if frame[0] in by_frame]
    count = len(pairs)
    if not count:
        raise RuntimeError("empty movie")
    errors = []
    for a, b in pairs:
        errors.append(sum((a[axis] - b[axis]) ** 2 for axis in range(1, 4)) ** 0.5)
    final_a, final_b = pairs[-1]
    final_error = sum((final_a[axis] - final_b[axis]) ** 2 for axis in range(1, 4)) ** 0.5
    return {"framesCompared": count, "maxError": max(errors),
            "meanError": sum(errors) / len(errors), "finalError": final_error,
            "restMatch": reference["frames"][-1][0] == native["frames"][-1][0],
            "inCupMatch": reference["inCup"] == native["inCup"]}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--holes", default="1,6,9,12")
    parser.add_argument("--binary", default=str(ROOT / "tools" / "golf_test" / "out" / "golf_test"))
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    binary = Path(args.binary)
    if not binary.exists():
        raise SystemExit("build tools/golf_test first")
    results = []
    for hole_id in (int(value) for value in args.holes.split(",")):
        probe = HoleHarness(hole_id)
        tee, cup = probe.prepare()
        probe.delete()
        for case in make_cases(hole_id, tee, cup):
            reference = ode_movie(hole_id, case)
            native = native_movie(binary, hole_id, case, reference)
            result = {"hole": hole_id, "case": case, "ode": reference, "native": native}
            result["comparison"] = compare(reference, native)
            results.append(result)
    all_errors = [entry["comparison"] for entry in results]
    summary = {"putts": len(results),
               "maxError": max(item["maxError"] for item in all_errors),
               "meanError": sum(item["meanError"] for item in all_errors) / len(all_errors),
               "restMatches": sum(item["restMatch"] for item in all_errors),
               "inCupMatches": sum(item["inCupMatch"] for item in all_errors)}
    report = {"summary": summary, "putts": results}
    if args.out:
        args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print("ODE parity: putts={putts} max={maxError:.6f} mean={meanError:.6f} "
          "rest={restMatches}/{putts} in-cup={inCupMatches}/{putts}".format(**summary))


if __name__ == "__main__":
    main()
