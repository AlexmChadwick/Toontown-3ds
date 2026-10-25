"""Execute the pinned boss generateSuits methods through the real dev patch."""
import ast
import functools
import importlib.util
import os
from pathlib import Path
import random
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('ai_dev', ROOT / 'tools/server/ai_dev.py')
dev = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dev)
upstream = ROOT / 'ref/ref/open-toontown/toontown/suit'

class Harness:
    nerfed = False
    def invokeSuitPlanner(self, *args):
        return {'activeSuits': [SimpleNamespace(currHP=500, maxHP=500)],
                'reserveSuits': [(SimpleNamespace(currHP=700, maxHP=700), 100)]}
    def generateDinerSuits(self):
        return self.invokeSuitPlanner()

for weak in ('0', '1'):
    os.environ['AI_DEV_WEAK_COGS'] = weak
    for name in sorted(dev._BOSS_GENERATORS):
        short = name.rsplit('.', 1)[1]
        tree = ast.parse((upstream / (short + '.py')).read_text())
        cls = next(node for node in tree.body if isinstance(node, ast.ClassDef))
        method = next(node for node in cls.body if isinstance(node, ast.FunctionDef) and node.name == 'generateSuits')
        namespace = {'random': random, 'functools': functools, 'cmp': lambda a,b: (a>b)-(a<b),
                     'SuitBuildingGlobals': SimpleNamespace(SuitBuildingInfo=[None]*20),
                     'simbase': SimpleNamespace(config=SimpleNamespace(GetBool=lambda *args: False))}
        exec(compile(ast.Module(body=[method], type_ignores=[]), str(upstream / (short+'.py')), 'exec'), namespace)
        boss_type = type(short, (Harness,), {'generateSuits': namespace['generateSuits']})
        dev._patch(name, SimpleNamespace(**{short: boss_type}))
        for round_number in (1, 2, 3):
            result = boss_type().generateSuits(round_number)
            suits = result['activeSuits'] + [entry[0] for entry in result['reserveSuits']]
            assert suits
            assert all(suit.currHP == suit.maxHP == 1 for suit in suits) if weak == '1' else all(suit.currHP > 1 for suit in suits)
        print(short, 'weak' if weak == '1' else 'normal', 'PASS')
print('real boss generators: active/reserve HP and opt-out PASS')
