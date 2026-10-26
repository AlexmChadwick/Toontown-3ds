from pathlib import Path
root = Path(__file__).resolve().parents[2] / 'sdmc-out/racing2'
a = [[float(v) for v in row.split()] for row in (root/'pies-cpp.txt').read_text().splitlines()]
b = [[float(v) for v in row.split()] for row in (root/'pies-reference.txt').read_text().splitlines()]
assert len(a) == len(b) == 2
error = max(abs(x-y) for row,reference in zip(a,b) for x,y in zip(row,reference))
assert error < .02, (error,a,b)
print('Stock Panda pie oracle: targeted and untargeted 90-frame scenarios PASS; max difference',error)
