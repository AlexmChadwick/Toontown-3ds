"""Load upstream level spec modules (toontown/coghq/*Spec.py, *_Cogs.py) as plain data.

The specs are dict literals that import only `panda3d.core` (Point3/Vec3/Vec4),
TTLocalizer (zone descriptions) and SpecImports. They are executed here with stub
modules; no game code is imported. Callers own the Panda import (bake scripts run with
stock panda3d).
"""
import ast
import sys
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
COGHQ = ROOT / 'ref/open-toontown/toontown/coghq'


class _Localizer(types.ModuleType):
    def __getattr__(self, name):
        if name.startswith('__'):
            raise AttributeError(name)
        return name  # zone descriptions are not needed at runtime


def _namespace():
    import panda3d.core as core
    ns = {k: getattr(core, k) for k in dir(core) if not k.startswith('_')}
    return ns


def _strip_imports(src):
    """Drop import statements; everything they provide is preloaded in the namespace."""
    tree = ast.parse(src)
    tree.body = [n for n in tree.body if not isinstance(n, (ast.Import, ast.ImportFrom))]
    return compile(tree, '<spec>', 'exec')


_GLOBALS = None


def _toontown_globals():
    """toontown/toonbase/ToontownGlobals.py constants, executed statement by statement
    (statements needing game imports are skipped; specs only use plain constants)."""
    global _GLOBALS
    if _GLOBALS is None:
        ns = _namespace()
        ns['TTLocalizer'] = _Localizer('TTLocalizer')
        tree = ast.parse((ROOT / 'ref/open-toontown/toontown/toonbase/ToontownGlobals.py').read_text())
        for node in tree.body:
            if isinstance(node, (ast.Import, ast.ImportFrom)):
                continue
            try:
                exec(compile(ast.Module([node], []), '<ToontownGlobals>', 'exec'), ns)
            except Exception:
                pass
        _GLOBALS = types.SimpleNamespace(**{k: v for k, v in ns.items() if not k.startswith('_')})
    return _GLOBALS


def load_module(name, directory=COGHQ):
    path = Path(directory) / f'{name}.py'
    ns = _namespace()
    ns['TTLocalizer'] = _Localizer('TTLocalizer')
    ns['ToontownGlobals'] = _toontown_globals()
    import random
    ns['random'] = random.Random(0)  # only debug 'titleString's use it; keeps bakes deterministic
    ns['__name__'] = name
    exec(_strip_imports(path.read_text()), ns)
    return ns


def load_spec(name, scenario=0):
    """Returns {entId: dict} merged from globalEntities and the scenario."""
    ns = load_module(name)
    spec = ns['levelSpec']
    ents = dict(spec['globalEntities'])
    scenarios = spec.get('scenarios', [{}])
    if scenarios:
        ents.update(scenarios[scenario])
    return ents


def load_cogs(name):
    ns = load_module(name)
    return {'CogData': ns.get('CogData', []), 'ReserveCogData': ns.get('ReserveCogData', []),
            'BattleCells': ns.get('BattleCells', {})}
