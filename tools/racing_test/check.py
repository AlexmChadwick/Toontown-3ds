from pathlib import Path
root = Path(__file__).resolve().parents[2] / 'sdmc-out/racing2'
a = [[float(v) for v in line.split()] for line in (root / 'physics-cpp.txt').read_text().splitlines()]
b = [[float(v) for v in line.split()] for line in (root / 'physics-reference.txt').read_text().splitlines() if line and line[0].isdigit()]
assert len(a) == len(b) == 6
error = max(abs(x-y) for row, reference in zip(a,b) for x,y in zip(row,reference))
assert error < .04, error
print('Stock Panda physics oracle: six 300-frame scenarios PASS; max component difference', error)
