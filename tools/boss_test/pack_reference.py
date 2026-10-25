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
for name in ('DistributedLawbotCannon', 'DistributedLawbotChair',
             'DistributedLawbotBossGavel', 'DistributedLawbotBossSuit'):
    cls = dc.getClassByName(name)
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is None and (field.isRequired() or field.isRam()):
            records.append((cls.getNumber(), field.getNumber(), bytes(field.getDefaultValue())))
    samples = {'setMovie': '(1,42,12)', 'setCannonWillFire': '(42,0,-25,35,-32760)',
               'updateCannonPosition': '(42,50,85)', 'setToonJurorIndex': '(-1)',
               'setState': '(78)', 'doAttack': '(1,2,3,100,20,1.3)',
               'setPosHpr': '(-80,10,0,-90,0,0)'}
    for field_name, value in samples.items():
        field = cls.getFieldByName(field_name)
        if field is not None:
            blob = bytes(field.parseString(value))
            assert blob
            records.append((cls.getNumber(), field.getNumber(), blob))
ceo_boss = dc.getClassByName('DistributedBossbotBoss')
for field_name, value in [('setSpeedDamage', '(25, 20, -32760)'),
                          ('setBossDamage', '(120, 20, -32760)'),
                          ('toonGotFood', '(42, 0, 1, 10)'),
                          ('toonServeFood', '(42, 2, 3)'),
                          ('toonGotToonup', '(42, 1, 2, 11)'),
                          ('setBattleDifficulty', '(1)'),
                          ('reachedTable', '(3)'),
                          ('hitTable', '(3)'),
                          ('awayFromTable', '(3)')]:
    field = ceo_boss.getFieldByName(field_name)
    blob = bytes(field.parseString(value))
    assert blob
    records.append((ceo_boss.getNumber(), field.getNumber(), blob))
for name in ('DistributedBanquetTable', 'DistributedGolfSpot', 'DistributedFoodBelt'):
    cls = dc.getClassByName(name)
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is None and (field.isRequired() or field.isRam()):
            records.append((cls.getNumber(), field.getNumber(), bytes(field.getDefaultValue())))
    samples = [('setIndex', '(1)'),
               ('setBossCogId', '(100)'),
               ('setNumDiners', '(8)'),
               ('setDinerInfo', '([1,1,1,1,1,1,1,1],[2,2,2,2,2,2,2,2],[8,9,8,9,8,9,8,9])'),
               ('setDinerStatus', '(3, 1)'),
               ('setState', '(67, 42, 0)' if name != 'DistributedFoodBelt' else '(78)'),
               ('setPitcherPos', '(255, 180, -32760)'),
               ('clearSmoothing', '(0)'),
               ('firingWater', '(0, 10, 2, 0, 50, 2)'),
               ('waterHitBoss', '(3)'),
               ('setGolfSpotPos', '(255, 45, -32760)'),
               ('setSwingInfo', '(80, 45, 1)'),
               ('setGoingToReward', '()')]
    for field_name, value in samples:
        field = cls.getFieldByName(field_name)
        if field is not None:
            blob = bytes(field.parseString(value))
            assert blob is not None
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
# Independent required+RAM child enters, including embedded-NUL suit DNA.
cj_enters = []
for offset, name in enumerate(('DistributedLawbotCannon', 'DistributedLawbotChair',
                               'DistributedLawbotBossGavel', 'DistributedLawbotBossSuit')):
    cls = dc.getClassByName(name)
    required = []
    optional = []
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is not None or not (field.isRequired() or field.isRam()):
            continue
        value = bytes(field.getDefaultValue())
        samples = {'setBossCogId': '(100)', 'setIndex': '(3)', 'setState': '(78)',
                   'setPosHpr': '(-80,10,0,-90,0,0)'}
        if field.getName() in samples:
            value = bytes(field.parseString(samples[field.getName()]))
        if field.isRequired():
            required.append(value)
        else:
            optional.append(struct.pack('<H', field.getNumber()) + value)
    payload = b''.join(required) + struct.pack('<H', len(optional)) + b''.join(optional)
    cj_enters.append(struct.pack('<IHI', 700 + offset, cls.getNumber(), len(payload)) + payload)
(out / 'cj_enters.bin').write_bytes(b''.join(cj_enters))
ceo_enters = []
for offset, name in enumerate(('DistributedBanquetTable', 'DistributedGolfSpot', 'DistributedFoodBelt')):
    cls = dc.getClassByName(name)
    required = []
    optional = []
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is not None or not (field.isRequired() or field.isRam()):
            continue
        value = bytes(field.getDefaultValue())
        samples = {
            'setBossCogId': '(100)',
            'setIndex': '(1)',
            'setNumDiners': '(8)',
            'setDinerInfo': '([1,1,1,1,1,1,1,1],[2,2,2,2,2,2,2,2],[8,9,8,9,8,9,8,9])',
            'setState': '(67,42,0)' if name != 'DistributedFoodBelt' else '(78)',
        }
        if field.getName() in samples:
            value = bytes(field.parseString(samples[field.getName()]))
        if field.isRequired():
            required.append(value)
        else:
            optional.append(struct.pack('<H', field.getNumber()) + value)
    payload = b''.join(required) + struct.pack('<H', len(optional)) + b''.join(optional)
    ceo_enters.append(struct.pack('<IHI', 800 + offset, cls.getNumber(), len(payload)) + payload)
(out / 'ceo_enters.bin').write_bytes(b''.join(ceo_enters))

