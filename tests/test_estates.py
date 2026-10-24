#!/usr/bin/env python3
import importlib.util, pathlib, struct, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
dc = ROOT / "source/net/dc_generated.h"
text = dc.read_text()
assert "547155168u" in text
assert "namespace DistributedEstate" in text
assert "namespace DistributedPet" in text

upstream = pathlib.Path("/Users/alexchadwick/toontown-3ds/ref/open-toontown")
spec = importlib.util.spec_from_file_location("petdna", upstream / "toontown/pets/PetDNA.py")
mod = importlib.util.module_from_spec(spec)
sys.path.insert(0, str(upstream))
try:
    spec.loader.exec_module(mod)
except Exception:
    # The table source is still checked directly when Panda imports are unavailable.
    source = (upstream / "toontown/pets/PetDNA.py").read_text()
    assert "NumFields = 9" in source and "'body': 4" in source
else:
    assert mod.NumFields == 9 and mod.Fields["body"] == 4
print("estate DC declarations and upstream PetDNA contract: PASS")
