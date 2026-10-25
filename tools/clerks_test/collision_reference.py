from pathlib import Path
import sys
import runpy

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/pipeline"))
import bake_collision

bake_collision.DEFAULT_RESOURCES = str(ROOT / "ref/ref/resources")
runpy.run_path(
    str(ROOT / "tools/collision_test/panda_reference.py"), run_name="__main__"
)
