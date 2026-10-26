#!/usr/bin/env python3
"""Reuse the verified Kart attachment baker for all three shipped LODs."""
import argparse
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--lod', type=int, choices=(0, 1, 2), required=True)
args = parser.parse_args()
source = ROOT / 'tools/pipeline/bake_clerk_karts.py'
tree = ast.parse(source.read_text())
class RacingBake(ast.NodeTransformer):
    def visit_Call(self, node):
        self.generic_visit(node)
        if isinstance(node.func, ast.Attribute) and node.func.attr == 'getKartModelPath' and len(node.args) == 2:
            node.args[1] = ast.Constant(args.lod)
        return node
    def visit_Expr(self, node):
        # Keep the clerk manifest and assets untouched; this run owns its LOD directory.
        if isinstance(node.value, ast.Call) and isinstance(node.value.func, ast.Attribute) and node.value.func.attr == 'write_text' and 'clerk_karts.json' in ast.unparse(node.value.func.value):
            return None
        return self.generic_visit(node)
namespace = {'__file__': str(source), '__name__': 'racing_kart_baker'}
exec(compile(ast.fix_missing_locations(RacingBake().visit(tree)), str(source), 'exec'), namespace)
namespace['OUT'] = ROOT / 'sdmc-out/3ds/toontown3ds/data/racing/karts' / ('lod%d' % args.lod)
namespace['main']()
