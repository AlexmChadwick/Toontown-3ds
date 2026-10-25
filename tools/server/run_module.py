"""Isolated local server entry; paths and bytecode stay in this worktree."""
import os
from pathlib import Path
import runpy
import sys

root = Path(__file__).resolve().parents[2]
upstream = root / 'ref/open-toontown'
if not (upstream / 'toontown').is_dir():
    upstream = root / 'ref/ref/open-toontown'
sys.path.insert(0, str(upstream))
sys.dont_write_bytecode = True  # -I ignores PYTHONPYCACHEPREFIX; ref is read-only
module = sys.argv.pop(1)
if module == '--dev-ai':
    sys.path.insert(0, str(root / 'tools/server'))
    runpy.run_path(str(root / 'tools/server/ai_dev.py'), run_name='__main__')
else:
    runpy.run_module(module, run_name='__main__', alter_sys=True)
