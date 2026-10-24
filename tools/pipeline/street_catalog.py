"""Verified DNA/storage catalog; resource paths follow docs/design/zones.md."""
# hood: (code, phase, DNA basename, street branch numbers)
HOODS = {
    1000: ('DD',6,'donalds_dock',(1100,1200,1300)),
    2000: ('TT',4,'toontown_central',(2100,2200,2300)),
    3000: ('BR',8,'the_burrrgh',(3100,3200,3300)),
    4000: ('MM',6,'minnies_melody_land',(4100,4200,4300)),
    5000: ('DG',8,'daisys_garden',(5100,5200,5300)),
    6000: ('OZ',6,'outdoor_zone',()),
    8000: ('GS',6,'goofy_speedway',()),
    9000: ('DL',8,'donalds_dreamland',(9100,9200)),
}
ZONES = tuple(sorted(z for hood,(_,_,_,streets) in HOODS.items() for z in (hood,*streets)))


def paths(zone):
    hood=zone-zone%1000
    code,phase,name,streets=HOODS[hood]
    playground=zone==hood
    if not playground and zone not in streets:
        raise ValueError('unsupported zone '+str(zone))
    zone_phase=phase if playground or hood!=2000 else 5
    dna=f'phase_{zone_phase}/dna/{name}_{"sz" if playground else zone}.dna'
    storage=['phase_4/dna/storage.dna',f'phase_{phase}/dna/storage_{code}.dna']
    if playground:
        storage.append(f'phase_{phase}/dna/storage_{code}_sz.dna')
    else:
        storage.extend(['phase_5/dna/storage_town.dna',f'phase_{zone_phase}/dna/storage_{code}_town.dna'])
    return playground,storage,dna
