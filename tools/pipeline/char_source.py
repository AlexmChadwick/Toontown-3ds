"""Read pinned character tables without importing the upstream game client."""
import ast
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / 'ref/open-toontown'
RESOURCES = ROOT / 'ref/resources'
def _names():
    tree = ast.parse((UPSTREAM / 'toontown/char/CharDNA.py').read_text())
    codes = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                 and isinstance(n.targets[0], ast.Name) and n.targets[0].id == 'charTypes')
    cls = next(n for n in tree.body if isinstance(n, ast.ClassDef) and n.name == 'CharDNA')
    method = next(n for n in cls.body if isinstance(n, ast.FunctionDef) and n.name == 'getCharName')
    namespace = {}
    exec(compile(ast.Module(body=[method],type_ignores=[]),'CharDNA.getCharName','exec'),namespace)
    return {code: namespace['getCharName'](SimpleNamespace(name=code)) for code in codes}


NAMES = _names()
HEIGHTS = dict(zip(NAMES, (3,3,3,3,4.8,4.8,4.5,4.5,4.5,3,4.5,3,4.5,4.5,2,2,2,2)))

def tables():
    tree = ast.parse((UPSTREAM / 'toontown/char/Char.py').read_text())
    out = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            key = node.targets[0].id
            if key in ('AnimDict', 'ModelDict', 'LODModelDict'):
                out[key] = ast.literal_eval(node.value)
    assert set(out['ModelDict']) == set(NAMES)
    return out


def path_namespace():
    # Execute only the pinned data/helper module, excluding all imports. It uses
    # Panda points, copy, and localized identifiers; no game package is imported.
    import copy
    from panda3d.core import Point3, Vec3
    tree = ast.parse((UPSTREAM / 'toontown/classicchars/CCharPaths.py').read_text())
    tree.body = [n for n in tree.body if not isinstance(n, (ast.Import, ast.ImportFrom))]
    identifiers = {n.attr for n in ast.walk(tree) if isinstance(n, ast.Attribute)
                   and isinstance(n.value, ast.Name) and n.value.id == 'TTLocalizer'}
    ns = dict(Point3=Point3, Vec3=Vec3, copy=copy,
              TTLocalizer=SimpleNamespace(**{n:n for n in identifiers}))
    exec(compile(tree, 'CCharPaths.py', 'exec'), ns)
    return ns
