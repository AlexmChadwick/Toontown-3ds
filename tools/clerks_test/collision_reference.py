from pathlib import Path
import sys
import runpy

ROOT = Path(__file__).resolve().parents[2]
# Worktrees link the main checkout's ref/ as ref/ref; the main checkout has it at ref/.
REF = ROOT / 'ref/ref' if (ROOT / 'ref/ref').exists() else ROOT / 'ref'
sys.path.insert(0, str(ROOT / "tools/pipeline"))
import bake_collision

bake_collision.DEFAULT_RESOURCES = str(REF / "resources")
runpy.run_path(
    str(ROOT / "tools/collision_test/panda_reference.py"), run_name="__main__"
)
