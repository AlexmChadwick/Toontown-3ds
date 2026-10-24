#!/usr/bin/env python3
"""Launch the existing toon baker with the read-only reference layout resolved.

Pass --jobs 1 (or at most 4), never --boot-dir for this task. The existing baker's
geometry and legacy animation functions are unchanged.
"""
import sys
sys.dont_write_bytecode = True
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from battle_common import RES, UP
sys.path.insert(0,str(UP))
import bake_toon
bake_toon.RESOURCES = str(RES)
bake_toon.UPSTREAM = str(UP)
bake_toon.__file__ = __file__
if __name__ == '__main__':
    bake_toon.main()
