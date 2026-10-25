"""Independent Panda DC streams and seeded maze choices; Python -I -B."""
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode = True
from panda3d.direct import DCFile
from direct.showbase.RandomNumGen import RandomNumGen
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'sdmc-out/cogdo-test'
OUT.mkdir(parents=True,exist_ok=True)
dc = DCFile()
for name in ('otp.dc','toon.dc'):
    assert dc.read(str(ROOT / 'ref/ref/open-toontown/etc' / name))
assert dc.getHash() == 547155168
names = ['DistributedCogdoInterior','DistributedCogdoBarrel','DistCogdoMazeGame','DistCogdoFlyingGame',
         'DistCogdoBoardroomGame','DistCogdoCraneGame','DistCogdoCrane','DistCogdoCraneMoneyBag','DistCogdoCraneCog']
records = []
enters = []
for offset,name in enumerate(names):
    cls = dc.getClassByName(name)
    required = []
    optional = []
    for index in range(cls.getNumInheritedFields()):
        field = cls.getInheritedField(index)
        if field.asMolecularField() is not None or not (field.isRequired() or field.isRam()):
            continue
        value = bytes(field.getDefaultValue())
        records.append(struct.pack('<HHI',cls.getNumber(),field.getNumber(),len(value))+value)
        if field.isRequired(): required.append(value)
        else: optional.append(struct.pack('<H',field.getNumber())+value)
    data = b''.join(required)+struct.pack('<H',len(optional))+b''.join(optional)
    enters.append(struct.pack('<IHI',1000+offset,cls.getNumber(),len(data))+data)
for name,field_name,value in [
    ('DistributedCogdoInterior','setToons','([42,43,0,0],0)'),
    ('DistributedCogdoInterior','setSuits','([50,51],[52,53],[100,50])'),
    ('DistributedCogdoInterior','setState','("WaitForAllToonsInside",-32760)'),
    ('DistCogdoMazeGame','setNumSuits','([4,5,5])'),
    ('DistCogdoMazeGame','doAction','(2,0,-32760)'),
    ('DistCogdoMazeGame','toonUsedGag','(42,1.5,-2,90,-32760)'),
    ('DistCogdoCrane','setCablePos','(255,24.5,315,[(1,2,-6),(2,3,-13),(3,4,-20)],-32760)')]:
    cls = dc.getClassByName(name)
    field = cls.getFieldByName(field_name)
    data = bytes(field.parseString(value))
    assert data
    records.append(struct.pack('<HHI',cls.getNumber(),field.getNumber(),len(data))+data)
(OUT/'vectors.bin').write_bytes(b''.join(records))
(OUT/'enters.bin').write_bytes(b''.join(enters))
seeds = []
for seed in [0,1,42,1000,400000001,4294967295]:
    parent = RandomNumGen(seed)
    rng = RandomNumGen(parent)
    barriers = list(range(12))
    rng.shuffle(barriers)
    quadrants = list(range(9))
    rng.shuffle(quadrants)
    angles = [rng.randint(0,3) for _ in range(9)]
    seeds.append(struct.pack('<I12B9B9B',seed,*barriers,*quadrants,*angles))
(OUT/'maze_seeds.bin').write_bytes(b''.join(seeds))
print('Panda Cogdo fields:',len(records),'child enters:',len(enters),'maze seeds:',len(seeds))
