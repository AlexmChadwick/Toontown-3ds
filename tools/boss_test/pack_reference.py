#!/usr/bin/env python3
"""Stock Panda DC vectors, independent of dcgen's C++ descriptors."""
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from panda3d.direct import DCFile
ROOT = Path(__file__).resolve().parents[2]
# ref/ in the main checkout; agent worktrees link it as ref/ref.
ETC = next(p for p in (ROOT / 'ref/open-toontown/etc', ROOT / 'ref/ref/open-toontown/etc') if (p / 'otp.dc').exists())
dc = DCFile()
for name in ('otp.dc', 'toon.dc'):
    assert dc.read(str(ETC / name))
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
for name in ('DistributedCashbotBossCrane', 'DistributedCashbotBossSafe',
             'DistributedCashbotBossGoon', 'DistributedCashbotBossTreasure'):
    cls = dc.getClassByName(name)
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is None and (field.isRequired() or field.isRam()):
            records.append((cls.getNumber(), field.getNumber(), bytes(field.getDefaultValue())))
    samples = [('setState', '(67,42)'),
               ('setObjectState', '(68,42,300)'),
               ('setCablePos', '(255,24.5,315,[(1,2,-6),(2,3,-13),(3,4,-20)],-32760)'),
               ('setupGoon', '(4.5,70,15,8,1.5)'),
               ('setTarget', '(120,-315,359.5,-32760)'),
               ('setSmXYZH', '(120,-315,10,90,-32760)'),
               ('setSmPosHprL', '(123,120,-315,10,90,0,0,-32760)')]
    for field_name, value in samples:
        field = cls.getFieldByName(field_name)
        if field is None:
            continue
        blob = bytes(field.parseString(value))
        assert blob, (name, field_name, value)
        records.append((cls.getNumber(), field.getNumber(), blob))
out = ROOT / 'sdmc-out/boss-test'
out.mkdir(parents=True, exist_ok=True)
(out / 'panda_vectors.bin').write_bytes(b''.join(struct.pack('<HHI', cls, field, len(blob)) + blob
                                               for cls, field, blob in records))
enters = []
for offset, name in enumerate(('DistributedCashbotBossCrane', 'DistributedCashbotBossSafe',
                               'DistributedCashbotBossGoon', 'DistributedCashbotBossTreasure')):
    cls = dc.getClassByName(name)
    required = []
    optional = []
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is not None:
            continue
        if not (field.isRequired() or field.isRam()):
            continue
        value = bytes(field.getDefaultValue())
        if field.getName() == 'setBossCogId':
            value = bytes(field.parseString('(100)'))
        if field.getName() == 'setIndex':
            value = bytes(field.parseString('(3)'))
        if field.getName() == 'setState':
            value = bytes(field.parseString('(67,42)'))
        if field.getName() == 'setObjectState':
            value = bytes(field.parseString('(68,42,300)'))
        if field.isRequired():
            required.append(value)
        else:
            optional.append(struct.pack('<H', field.getNumber()) + value)
    payload = b''.join(required) + struct.pack('<H', len(optional)) + b''.join(optional)
    enters.append(struct.pack('<IHI', 400 + offset, cls.getNumber(), len(payload)) + payload)
(out / 'cfo_enters.bin').write_bytes(b''.join(enters))
print(f'{len(records)} independent Panda boss field vectors')
