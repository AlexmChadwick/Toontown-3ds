"""Panda DC packing oracle for every racing sender. Run Python -I -B."""
from pathlib import Path
import struct
from panda3d.direct import DCFile
root = Path(__file__).resolve().parents[2]
# The main checkout has ref/<submodule>; linked worktrees reach it through ref/ref.
REF = root / 'ref' if (root / 'ref/open-toontown/toontown').is_dir() else root / 'ref/ref'
dc = DCFile()
for name in ('otp.dc', 'toon.dc'):
    assert dc.read(str(REF / 'open-toontown/etc' / name))
values = [
 ('DistributedStartingBlock','requestEnter','(0)'),
 ('DistributedStartingBlock','requestExit','()'),
 ('DistributedStartingBlock','movieFinished','()'),
 ('DistributedVehicle','requestControl','()'),
 ('DistributedVehicle','requestParked','()'),
 ('DistributedRace','requestKart','()'),
 ('DistributedObject','setBarrierReady','(123)'),
 ('DistributedRace','heresMyT','(42, 2, 0.25, 100)'),
 ('DistributedRace','requestThrow','(1.25, -2.5, 3.75)'),
 ('DistributedRace','hasGag','(3, 2, 4)'),
 ('DistributedSmoothNode','setSmPosHpr','(1.2,-2.3,4.5,90,0,0,100)'),
 ('DistributedGag','hitSomebody','(42,100)'),
 ('DistributedProjectile','hitSomebody','(42,100)'),
 ('DistributedRace','racerLeft','(42)'),
]
output = bytearray()
for cls, name, text in values:
    field = dc.getClassByName(cls).getFieldByName(name)
    data = field.parseString(text)
    assert data or text == '()', (cls,name,text)
    output += struct.pack('<HH',field.getNumber(),len(data)) + data
(root / 'sdmc-out/racing2/vectors.bin').write_bytes(output)
print('Panda DC oracle:', len(values), 'outgoing messages')
