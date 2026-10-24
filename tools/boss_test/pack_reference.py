#!/usr/bin/env python3
"""Stock Panda DC vectors, independent of dcgen's C++ descriptors."""
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from panda3d.direct import DCFile
ROOT = Path(__file__).resolve().parents[2]
dc = DCFile()
for name in ('otp.dc', 'toon.dc'):
    assert dc.read(str(ROOT / 'ref/ref/open-toontown/etc' / name))
assert dc.getHash() == 547155168
records = []
for name in ('DistributedSellbotBoss', 'DistributedCashbotBoss', 'DistributedLawbotBoss', 'DistributedBossbotBoss'):
    cls = dc.getClassByName(name)
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is not None:
            continue
        if field.isRequired() or field.isRam():
            records.append((cls.getNumber(), field.getNumber(), bytes(field.getDefaultValue())))
    for field_name, value in [('setState', '("BattleThree")'),
                              ('setToonIds', '([42,43,44,45,46,47,48,49], [42,43,44,45], [46,47,48,49])'),
                              ('setAttackCode', '(12,42)'),
                              ('setBarrierData', '([(65535,"Introduction",[42,43])])')]:
        field = cls.getFieldByName(field_name)
        blob = bytes(field.parseString(value))
        assert blob, (name, field_name, value)
        records.append((cls.getNumber(), field.getNumber(), blob))
out = ROOT / 'sdmc-out/boss-test'
out.mkdir(parents=True, exist_ok=True)
(out / 'panda_vectors.bin').write_bytes(b''.join(struct.pack('<HHI', cls, field, len(blob)) + blob
                                               for cls, field, blob in records))
print(f'{len(records)} independent Panda boss field vectors')
